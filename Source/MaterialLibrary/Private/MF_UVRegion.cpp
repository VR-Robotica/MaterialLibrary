// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Vrr UV Region: picks one material region out of a TechArt Toolbox "UV Layouts" channel, or tells which region a UV is in.
 *
 * Strategy
 *  The layout tool (TechArt Toolbox, Core_UVLayout.cpp) packs each material slot into its own rectangle of the 0-1
 *  square (atlas) or into its own UDIM tile. This node rebuilds the same rectangles in the shader from just Region and
 *  Region Count, so a material can work on one slot's area without any baked lookup data:
 *   - Bounds: Min and Size of the chosen region (CompileRegionBounds).
 *   - Every other output is simple math on the bounds: remap into the region, out of it, or test against it.
 *   - Index runs the layout backwards: from a UV to the region it falls in (CompileIndex).
 *  The row/column math is done with compiler ops (not C++) so Region and Region Count can be scalar parameters that
 *  material instances change; when they are constants, it all folds away at compile time.
 *  The row and column rules must stay in step with MakeRegions and MakeUDIMTiles in Core_UVLayout.cpp.
 *
 * Outputs
 *  0 Local UV   (UV - Min) / Size: 0-1 across the chosen region
 *  1 Mask       1 inside the chosen region, 0 elsewhere
 *  2 To Region  Min + UV * Size: places a 0-1 UV into the chosen region
 *  3 Bounds     (Min.U, Min.V, Size.U, Size.V)
 *  4 Index      the region the UV itself is in, whatever Region is set to
 */

#include "MF_UVRegion.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

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
	InitNode(TEXT("Vrr UV Region"), {
		{ TEXT("Local UV"), MCT_Float2 },
		{ TEXT("Mask"), MCT_Float1 },
		{ TEXT("To Region"), MCT_Float2 },
		{ TEXT("Bounds"), MCT_Float4 },
		{ TEXT("Index"), MCT_Float1 }
	});
}

#if WITH_EDITOR
/**
 * Steps
 *  1. Static UDIM switch (picks atlas or UDIM math).
 *  2. Index output: needs only the UV, not the chosen region.
 *  3. Bounds of the chosen region; every other output is built from them.
 *  4. To Region / Local UV / Mask from the UV and the bounds.
 */
int32 UMaterialExpressionVrrUVRegion::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// 1. Atlas or UDIM layout
	bool bUseUDIM = false;
	if (!GetUseUDIM(Compiler, bUseUDIM))
	{
		return INDEX_NONE;
	}

	// 2. Which region the UV is in
	if (OutputIndex == VrrUVRegion::Index)
	{
		return CompileIndex(Compiler, bUseUDIM);
	}

	// 3. Rectangle of the chosen region
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

	// 4a. To Region: 0-1 UV -> inside the region
	const int32 UV = CompileUV(Compiler);
	if (OutputIndex == VrrUVRegion::ToRegion)
	{
		return Compiler->Add(Min, Compiler->Mul(UV, Size));
	}

	// 4b. Local UV: region -> 0-1
	const int32 Local = Compiler->Div(Compiler->Sub(UV, Min), Size);
	if (OutputIndex == VrrUVRegion::LocalUV)
	{
		return Local;
	}

	// 4c. Mask: inside when 0 <= Local < 1 on both axes (the far edge belongs to the next region)
	const int32 Zero = Compiler->Constant(0.0f);
	const int32 One = Compiler->Constant(1.0f);
	const int32 Inside = Compiler->Mul(Compiler->Step(Zero, Local), Compiler->Sub(One, Compiler->Step(One, Local)));
	return Compiler->Mul(VrrMaterialMath::X(Compiler, Inside), VrrMaterialMath::Y(Compiler, Inside));
}

/**
 * UDIM In if connected, else the UDIM property. UDIM changes the shader's structure, so a connected value must be
 * static (known at compile time); otherwise this reports an error and returns false.
 */
bool UMaterialExpressionVrrUVRegion::GetUseUDIM(FMaterialCompiler* Compiler, bool& bOutUDIM)
{
	bOutUDIM = bUDIM;
	if (VrrMaterialMath::IsConnected(UDIMIn))
	{
		bool bSucceeded = false;
		bOutUDIM = Compiler->GetStaticBoolValue(UDIMIn.Compile(Compiler), bSucceeded);
		if (!bSucceeded)
		{
			NodeError(Compiler, TEXT("UDIM needs a static bool (a Static Bool or Static Bool Parameter)."));
			return false;
		}
	}
	return true;
}

/** The UV the node works on: Coordinates (first two components), or the Coordinate Index texture coordinate. */
int32 UMaterialExpressionVrrUVRegion::CompileUV(FMaterialCompiler* Compiler)
{
	return VrrMaterialMath::IsConnected(Coordinates)
		? Compiler->ComponentMask(Coordinates.Compile(Compiler), true, true, false, false)
		: Compiler->TextureCoordinate(static_cast<uint32>(FMath::Clamp(CoordinateIndex, 0, 7)), false, false);
}

/** Region Count as a whole number of at least 1 (a parameter may hold 2.9999). */
int32 UMaterialExpressionVrrUVRegion::CompileRegionCount(FMaterialCompiler* Compiler)
{
	const int32 Count = VrrMaterialMath::InputOr(Compiler, RegionCountIn, static_cast<float>(RegionCount));
	return Count == INDEX_NONE ? INDEX_NONE : Compiler->Max(Compiler->Round(Count), Compiler->Constant(1.0f));
}

/**
 * How Count regions are spread over rows: at most MaxColumns per row, and the rows as even as possible with the
 * longer rows first (5 = 3 + 2, 7 = 3 + 2 + 2).
 *   Rows  = ceil(N / MaxColumns)
 *   Base  = floor(N / Rows)          regions in a short row
 *   Extra = N - Base * Rows          the first Extra rows hold Base + 1
 *   Big   = Extra * (Base + 1)       regions in those long rows
 */
void UMaterialExpressionVrrUVRegion::CompileAtlasRows(FMaterialCompiler* Compiler, int32 Count, int32& OutRows, int32& OutBase, int32& OutExtra, int32& OutBig)
{
	// Whole numbers: the -0.0001 inside the ceil and the +0.5 inside the floor keep float division from landing
	// just past an integer
	const int32 One = Compiler->Constant(1.0f);
	const int32 Columns = Compiler->Constant(static_cast<float>(FMath::Max(MaxColumns, 1)));
	OutRows = Compiler->Ceil(Compiler->Sub(Compiler->Div(Count, Columns), Compiler->Constant(0.0001f)));
	OutBase = Compiler->Floor(Compiler->Div(Compiler->Add(Count, Compiler->Constant(0.5f)), OutRows));
	OutExtra = Compiler->Sub(Count, Compiler->Mul(OutBase, OutRows));
	OutBig = Compiler->Mul(OutExtra, Compiler->Add(OutBase, One));
}

/**
 * Min and Size of the chosen region (Region / Region In).
 *   UDIM:  Min = (i mod 10, -floor(i / 10)), Size = 1
 *   Atlas: Short  = i >= Big                  is it in one of the short rows?
 *          InRow  = Base + 1 - Short          regions in its row
 *          j      = i - Short * Big           index from the start of its kind of row
 *          Row    = floor(j / InRow) + Short * Extra
 *          Column = j - floor(j / InRow) * InRow
 *          Min = (Column / InRow, Row / Rows), Size = (1 / InRow, 1 / Rows)
 */
bool UMaterialExpressionVrrUVRegion::CompileRegionBounds(FMaterialCompiler* Compiler, bool bUseUDIM, int32& OutMin, int32& OutSize)
{
	const int32 Zero = Compiler->Constant(0.0f);
	const int32 One = Compiler->Constant(1.0f);
	const int32 Half = Compiler->Constant(0.5f);
	const int32 RawIndex = VrrMaterialMath::InputOr(Compiler, RegionIn, static_cast<float>(Region));
	if (RawIndex == INDEX_NONE)
	{
		return false;
	}

	// UDIM: one whole tile per region, ten to a row, later rows one unit down in V
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

	// Atlas
	// 1. Region index, clamped to the layout
	const int32 Count = CompileRegionCount(Compiler);
	if (Count == INDEX_NONE)
	{
		return false;
	}
	const int32 Index = Compiler->Clamp(Compiler->Round(RawIndex), Zero, Compiler->Sub(Count, One));

	// 2. Row layout
	int32 Rows, Base, Extra, Big;
	CompileAtlasRows(Compiler, Count, Rows, Base, Extra, Big);

	// 3. Row and column of the region: long rows come first, so count within long or short rows separately
	const int32 Short = Compiler->Step(Big, Index); // 1 when Index >= Big (a short row)
	const int32 InRow = Compiler->Sub(Compiler->Add(Base, One), Short);
	const int32 J = Compiler->Sub(Index, Compiler->Mul(Short, Big));
	const int32 RowInKind = Compiler->Floor(Compiler->Div(Compiler->Add(J, Half), InRow));
	const int32 Row = Compiler->Add(RowInKind, Compiler->Mul(Short, Extra));
	const int32 Column = Compiler->Sub(J, Compiler->Mul(RowInKind, InRow));

	// 4. Rectangle: each row is 1 / Rows tall, each region 1 / InRow wide
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
	const int32 U = VrrMaterialMath::X(Compiler, UV);
	const int32 V = VrrMaterialMath::Y(Compiler, UV);
	const int32 Zero = Compiler->Constant(0.0f);
	const int32 One = Compiler->Constant(1.0f);

	// UDIM: the tile the UV is in
	if (bUseUDIM)
	{
		const int32 Ten = Compiler->Constant(VrrUVRegion::UDIMColumns);
		const int32 Column = Compiler->Clamp(Compiler->Floor(U), Zero, Compiler->Sub(Ten, One));
		const int32 Row = Compiler->Max(Compiler->Sub(Zero, Compiler->Floor(V)), Zero);
		return Compiler->Add(Column, Compiler->Mul(Row, Ten));
	}

	// Atlas
	// 1. Row layout (same as CompileRegionBounds)
	const int32 Count = CompileRegionCount(Compiler);
	if (Count == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	int32 Rows, Base, Extra, Big;
	CompileAtlasRows(Compiler, Count, Rows, Base, Extra, Big);

	// 2. Row from V (all rows are the same height), then the column from U within that row's width
	const int32 Row = Compiler->Clamp(Compiler->Floor(Compiler->Mul(V, Rows)), Zero, Compiler->Sub(Rows, One));
	const int32 Short = Compiler->Step(Extra, Row); // 1 when Row >= Extra (a short row)
	const int32 BasePlusOne = Compiler->Add(Base, One);
	const int32 InRow = Compiler->Sub(BasePlusOne, Short);
	const int32 Column = Compiler->Clamp(Compiler->Floor(Compiler->Mul(U, InRow)), Zero, Compiler->Sub(InRow, One));

	// 3. Regions before this row (long rows hold Base + 1, short rows Base), plus the column
	const int32 Start = Compiler->Sub(Compiler->Mul(Row, BasePlusOne), Compiler->Mul(Short, Compiler->Sub(Row, Extra)));
	return Compiler->Add(Start, Column);
}

/** UDIM In takes a static bool, so the material editor only accepts static bool connections on that pin. */
EMaterialValueType UMaterialExpressionVrrUVRegion::GetInputValueType(int32 InputIndex)
{
	return InputIndex == VrrUVRegion::UDIMInputIndex ? MCT_StaticBool : Super::GetInputValueType(InputIndex);
}

#endif
