#include "MF_UVRegion.h"

#include "MaterialCompiler.h"

#define LOCTEXT_NAMESPACE "VrrMaterialExpressions"

namespace VrrUVRegion
{
	enum EOutput : int32
	{
		LocalUV = 0,
		Mask = 1,
		ToRegion = 2,
		Bounds = 3,
		Index = 4,
	};

	/** UDIM tiles per row, as in the UDIM standard (1001-1010). */
	constexpr float UDIMColumns = 10.0f;

	/** UDIM In is the fourth input (Coordinates, Region In, Region Count In, UDIM In). */
	constexpr int32 UDIMInputIndex = 3;
}

UMaterialExpressionVrrUVRegion::UMaterialExpressionVrrUVRegion(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(LOCTEXT("VRR", "VRR"));

	bShowOutputNameOnPin = true;
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Local UV")));
	Outputs.Add(FExpressionOutput(TEXT("Mask")));
	Outputs.Add(FExpressionOutput(TEXT("To Region")));
	Outputs.Add(FExpressionOutput(TEXT("Bounds")));
	Outputs.Add(FExpressionOutput(TEXT("Index")));
#endif
}

#if WITH_EDITOR
/**
 * Atlas region rectangle in the shader, so Region and Region Count can come from parameters:
 *   Rows   = ceil(N / MaxColumns)
 *   Base   = floor(N / Rows)          regions in a short row
 *   Extra  = N - Base * Rows          the first Extra rows hold Base + 1
 *   Big    = Extra * (Base + 1)       regions in those long rows
 *   Long   = i < Big
 *   InRow  = Base + Long
 *   j      = i - (1 - Long) * Big     index from the start of its kind of row
 *   Row    = floor(j / InRow) + (1 - Long) * Extra
 *   Column = j - floor(j / InRow) * InRow
 *   Min = (Column / InRow, Row / Rows), Size = (1 / InRow, 1 / Rows)
 * UDIM: Min = (i mod 10, -floor(i / 10)), Size = 1.
 * Constant inputs fold away at compile time.
 */
int32 UMaterialExpressionVrrUVRegion::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	bool bUseUDIM = false;
	if (!GetUseUDIM(Compiler, bUseUDIM))
	{
		return INDEX_NONE;
	}

	if (OutputIndex == VrrUVRegion::Index)
	{
		return CompileIndex(Compiler, bUseUDIM);
	}

	int32 Min = INDEX_NONE;
	int32 Size = INDEX_NONE;
	if (!CompileRegionBounds(Compiler, bUseUDIM, Min, Size))
	{
		return INDEX_NONE;
	}
	if (OutputIndex == VrrUVRegion::Bounds)
	{
		return Compiler->AppendVector(Min, Size);
	}

	const int32 UV = CompileUV(Compiler);
	if (OutputIndex == VrrUVRegion::ToRegion)
	{
		return Compiler->Add(Min, Compiler->Mul(UV, Size));
	}

	const int32 Local = Compiler->Div(Compiler->Sub(UV, Min), Size);
	if (OutputIndex == VrrUVRegion::LocalUV)
	{
		return Local;
	}

	// Mask: inside when 0 <= Local < 1 on both axes (the far edge belongs to the next region)
	const int32 Zero = Compiler->Constant(0.0f);
	const int32 One = Compiler->Constant(1.0f);
	const int32 Inside = Compiler->Mul(Compiler->Step(Zero, Local), Compiler->Sub(One, Compiler->Step(One, Local)));
	return Compiler->Mul(Compiler->ComponentMask(Inside, true, false, false, false), Compiler->ComponentMask(Inside, false, true, false, false));
}

bool UMaterialExpressionVrrUVRegion::GetUseUDIM(FMaterialCompiler* Compiler, bool& bOutUDIM)
{
	bOutUDIM = bUDIM;
	if (UDIMIn.GetTracedInput().Expression)
	{
		bool bSucceeded = false;
		bOutUDIM = Compiler->GetStaticBoolValue(UDIMIn.Compile(Compiler), bSucceeded);
		if (!bSucceeded)
		{
			Compiler->Errorf(TEXT("Vrr UV Region: UDIM needs a static bool (a Static Bool or Static Bool Parameter)."));
			return false;
		}
	}
	return true;
}

int32 UMaterialExpressionVrrUVRegion::CompileUV(FMaterialCompiler* Compiler)
{
	return Coordinates.GetTracedInput().Expression
		? Compiler->ComponentMask(Coordinates.Compile(Compiler), true, true, false, false)
		: Compiler->TextureCoordinate(static_cast<uint32>(FMath::Clamp(CoordinateIndex, 0, 7)), false, false);
}

int32 UMaterialExpressionVrrUVRegion::CompileRegionCount(FMaterialCompiler* Compiler)
{
	const int32 Count = RegionCountIn.GetTracedInput().Expression ? RegionCountIn.Compile(Compiler) : Compiler->Constant(static_cast<float>(RegionCount));
	return Count == INDEX_NONE ? INDEX_NONE : Compiler->Max(Compiler->Round(Count), Compiler->Constant(1.0f));
}

void UMaterialExpressionVrrUVRegion::CompileAtlasRows(FMaterialCompiler* Compiler, int32 Count, int32& OutRows, int32& OutBase, int32& OutExtra, int32& OutBig)
{
	// Whole numbers; the +0.5 inside the floor keeps float division from landing just under an integer
	const int32 One = Compiler->Constant(1.0f);
	const int32 Columns = Compiler->Constant(static_cast<float>(FMath::Max(MaxColumns, 1)));
	OutRows = Compiler->Ceil(Compiler->Sub(Compiler->Div(Count, Columns), Compiler->Constant(0.0001f)));
	OutBase = Compiler->Floor(Compiler->Div(Compiler->Add(Count, Compiler->Constant(0.5f)), OutRows));
	OutExtra = Compiler->Sub(Count, Compiler->Mul(OutBase, OutRows));
	OutBig = Compiler->Mul(OutExtra, Compiler->Add(OutBase, One));
}

bool UMaterialExpressionVrrUVRegion::CompileRegionBounds(FMaterialCompiler* Compiler, bool bUseUDIM, int32& OutMin, int32& OutSize)
{
	const int32 Zero = Compiler->Constant(0.0f);
	const int32 One = Compiler->Constant(1.0f);
	const int32 Half = Compiler->Constant(0.5f);
	const int32 RawIndex = RegionIn.GetTracedInput().Expression ? RegionIn.Compile(Compiler) : Compiler->Constant(static_cast<float>(Region));
	if (RawIndex == INDEX_NONE)
	{
		return false;
	}

	if (bUseUDIM)
	{
		const int32 Ten = Compiler->Constant(VrrUVRegion::UDIMColumns);
		const int32 Index = Compiler->Max(Compiler->Round(RawIndex), Zero);
		const int32 Row = Compiler->Floor(Compiler->Div(Compiler->Add(Index, Half), Ten));
		const int32 Column = Compiler->Sub(Index, Compiler->Mul(Row, Ten));
		OutMin = Compiler->AppendVector(Column, Compiler->Sub(Zero, Row));
		OutSize = Compiler->Constant2(1.0f, 1.0f);
		return true;
	}

	const int32 Count = CompileRegionCount(Compiler);
	if (Count == INDEX_NONE)
	{
		return false;
	}
	const int32 Index = Compiler->Clamp(Compiler->Round(RawIndex), Zero, Compiler->Sub(Count, One));

	int32 Rows, Base, Extra, Big;
	CompileAtlasRows(Compiler, Count, Rows, Base, Extra, Big);
	const int32 Short = Compiler->Step(Big, Index); // 1 when Index >= Big (a short row)
	const int32 InRow = Compiler->Sub(Compiler->Add(Base, One), Short);
	const int32 J = Compiler->Sub(Index, Compiler->Mul(Short, Big));
	const int32 RowInKind = Compiler->Floor(Compiler->Div(Compiler->Add(J, Half), InRow));
	const int32 Row = Compiler->Add(RowInKind, Compiler->Mul(Short, Extra));
	const int32 Column = Compiler->Sub(J, Compiler->Mul(RowInKind, InRow));

	OutMin = Compiler->AppendVector(Compiler->Div(Column, InRow), Compiler->Div(Row, Rows));
	OutSize = Compiler->AppendVector(Compiler->Div(One, InRow), Compiler->Div(One, Rows));
	return true;
}

/**
 * The region the UV is in.
 *   UDIM:  Column = floor(U) (0-9), Row = -floor(V) (0 for V in 0-1, 1 for V in -1-0, ...), Index = Column + 10 * Row
 *   Atlas: Row = floor(V * Rows), Short = Row >= Extra, InRow = Base + 1 - Short, Column = floor(U * InRow),
 *          Index = Row * (Base + 1) - Short * (Row - Extra) + Column     (rows and columns clamped to the layout)
 */
int32 UMaterialExpressionVrrUVRegion::CompileIndex(FMaterialCompiler* Compiler, bool bUseUDIM)
{
	const int32 UV = CompileUV(Compiler);
	if (UV == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	const int32 U = Compiler->ComponentMask(UV, true, false, false, false);
	const int32 V = Compiler->ComponentMask(UV, false, true, false, false);
	const int32 Zero = Compiler->Constant(0.0f);
	const int32 One = Compiler->Constant(1.0f);

	if (bUseUDIM)
	{
		const int32 Ten = Compiler->Constant(VrrUVRegion::UDIMColumns);
		const int32 Column = Compiler->Clamp(Compiler->Floor(U), Zero, Compiler->Sub(Ten, One));
		const int32 Row = Compiler->Max(Compiler->Sub(Zero, Compiler->Floor(V)), Zero);
		return Compiler->Add(Column, Compiler->Mul(Row, Ten));
	}

	const int32 Count = CompileRegionCount(Compiler);
	if (Count == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	int32 Rows, Base, Extra, Big;
	CompileAtlasRows(Compiler, Count, Rows, Base, Extra, Big);
	const int32 Row = Compiler->Clamp(Compiler->Floor(Compiler->Mul(V, Rows)), Zero, Compiler->Sub(Rows, One));
	const int32 Short = Compiler->Step(Extra, Row); // 1 when Row >= Extra (a short row)
	const int32 BasePlusOne = Compiler->Add(Base, One);
	const int32 InRow = Compiler->Sub(BasePlusOne, Short);
	const int32 Column = Compiler->Clamp(Compiler->Floor(Compiler->Mul(U, InRow)), Zero, Compiler->Sub(InRow, One));
	const int32 Start = Compiler->Sub(Compiler->Mul(Row, BasePlusOne), Compiler->Mul(Short, Compiler->Sub(Row, Extra)));
	return Compiler->Add(Start, Column);
}

EMaterialValueType UMaterialExpressionVrrUVRegion::GetInputValueType(int32 InputIndex)
{
	return InputIndex == VrrUVRegion::UDIMInputIndex ? MCT_StaticBool : MCT_Float;
}

void UMaterialExpressionVrrUVRegion::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Vrr UV Region"));
}
#endif

#undef LOCTEXT_NAMESPACE
