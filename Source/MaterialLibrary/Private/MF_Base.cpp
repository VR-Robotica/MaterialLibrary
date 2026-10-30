/*
 * Shared setup for the VRR material nodes: palette category, typed pin names, caption and error messages.
 * See MF_Base.h for why the nodes share a base.
 */

#include "MF_Base.h"

#include "EdGraph/EdGraphNode.h"
#include "MaterialCompiler.h"

#define LOCTEXT_NAMESPACE "VrrMaterialExpressions"

namespace
{
#if WITH_EDITORONLY_DATA
	/** "<Name> (<tag>)", or just the name for types without a tag. */
	FName TaggedName(const FString& Name, EMaterialValueType Type)
	{
		const TCHAR* Tag = UMaterialExpressionVrrBase::TypeTag(Type);
		return *Tag ? FName(*FString::Printf(TEXT("%s (%s)"), *Name, Tag)) : FName(*Name);
	}
#endif

#if WITH_EDITOR
	/** One input pin of a node class: the name it shows (None: leave it to the engine) and its type. */
	struct FInputPin
	{
		FName Name;
		EMaterialValueType Type = MCT_Unknown;
	};

	/** Type an input property expects, see MF_Base.h: its VrrType metadata, else its unwired constant. */
	EMaterialValueType InputTypeOf(const UClass* Class, const FStructProperty* Input)
	{
		// 1. Stated on the input
		static const FName VrrTypeKey(TEXT("VrrType"));
		if (const FString* Tag = Input->FindMetaData(VrrTypeKey))
		{
			static const TPair<const TCHAR*, EMaterialValueType> Tags[] = {
				{ TEXT("S"), MCT_Float1 }, { TEXT("V2"), MCT_Float2 }, { TEXT("V3"), MCT_Float3 }, { TEXT("V4"), MCT_Float4 }, { TEXT("SB"), MCT_StaticBool } };
			for (const TPair<const TCHAR*, EMaterialValueType>& Known : Tags)
			{
				if (*Tag == Known.Key)
				{
					return Known.Value;
				}
			}
		}

		// 2. The constant used while it's unwired
		static const FName OverridingKey(TEXT("OverridingInputProperty"));
		const FString InputName = Input->GetName();
		for (TFieldIterator<FProperty> It(Class); It; ++It)
		{
			const FString* Overrides = It->FindMetaData(OverridingKey);
			if (!Overrides || *Overrides != InputName)
			{
				continue;
			}
			if (It->IsA<FBoolProperty>())
			{
				return MCT_StaticBool;
			}
			if (It->IsA<FNumericProperty>())
			{
				return MCT_Float1;
			}
			if (const FStructProperty* Struct = CastField<FStructProperty>(*It))
			{
				const FName Type = Struct->Struct->GetFName();
				if (Type == NAME_LinearColor || Type == NAME_Color || Type == NAME_Vector || Type == NAME_Vector3f)
				{
					return MCT_Float3;
				}
				if (Type == NAME_Vector2D || Type == NAME_Vector2f)
				{
					return MCT_Float2;
				}
				if (Type == NAME_Vector4 || Type == NAME_Vector4f)
				{
					return MCT_Float4;
				}
			}
		}
		return MCT_Unknown;
	}

	/**
	 * A node class's input pins, in the engine's pin order (every FExpressionInput property, base class first).
	 * Worked out once per class: the editor asks for pin names and types every time it rebuilds a node.
	 */
	const TArray<FInputPin>& InputPinsOf(const UClass* Class)
	{
		static TMap<const UClass*, TUniquePtr<TArray<FInputPin>>> Cache;	// heap arrays: references stay valid as it grows
		if (const TUniquePtr<TArray<FInputPin>>* Known = Cache.Find(Class))
		{
			return **Known;
		}
		TArray<FInputPin>& Pins = *Cache.Add(Class, MakeUnique<TArray<FInputPin>>());
		for (TFieldIterator<FStructProperty> It(Class, EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated); It; ++It)
		{
			if (It->Struct->GetFName() != NAME_ExpressionInput)
			{
				continue;
			}
			if (It->ArrayDim > 1)
			{
				Pins.AddDefaulted(It->ArrayDim);	// input arrays: the engine names them "<Name>_<Index>"
				continue;
			}
			// "BaseColorIn" -> "Base Color (V3)"
			FString Name = It->GetName();
			if (Name.Len() > 2 && Name.EndsWith(TEXT("In"), ESearchCase::CaseSensitive))
			{
				Name.LeftChopInline(2);
			}
			const EMaterialValueType Type = InputTypeOf(Class, *It);
			Pins.Add({ TaggedName(FName::NameToDisplayString(Name, false), Type), Type });
		}
		return Pins;
	}
#endif
}

UMaterialExpressionVrrBase::UMaterialExpressionVrrBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Every VRR node is listed under "VRR" in the material palette
	MenuCategories.Add(LOCTEXT("VRR", "VRR"));
#endif
}

/**
 * Replaces the default single unnamed output with the node's named pins.
 * Pin names show on the node (bShowOutputNameOnPin), and Compile() receives the pin's position as OutputIndex.
 */
void UMaterialExpressionVrrBase::InitNode(const TCHAR* InCaption, std::initializer_list<FVrrOutput> InOutputs)
{
#if WITH_EDITORONLY_DATA
	Caption = InCaption;
	bShowOutputNameOnPin = true;
	OutputPins = InOutputs;
	RefreshOutputs();
#endif
}

const TCHAR* UMaterialExpressionVrrBase::TypeTag(EMaterialValueType Type)
{
	switch (Type)
	{
	case MCT_Float1:     return TEXT("S");
	case MCT_Float2:     return TEXT("V2");
	case MCT_Float3:     return TEXT("V3");
	case MCT_Float4:     return TEXT("V4");
	case MCT_StaticBool: return TEXT("SB");
	case MCT_Bool:       return TEXT("B");
	default:             return TEXT("");
	}
}

void UMaterialExpressionVrrBase::SetOutputType(int32 OutputIndex, EMaterialValueType Type)
{
#if WITH_EDITORONLY_DATA
	if (OutputPins.IsValidIndex(OutputIndex))
	{
		OutputPins[OutputIndex].Type = Type;
	}
#endif
}

bool UMaterialExpressionVrrBase::RefreshOutputs()
{
	bool bRenamed = false;
#if WITH_EDITORONLY_DATA
	UpdateOutputTypes();
	// Saved nodes carry the names they were saved with: rebuild from the current list (links use the index)
	Outputs.SetNum(OutputPins.Num());
	for (int32 Index = 0; Index < OutputPins.Num(); ++Index)
	{
		const FName Name = TaggedName(OutputPins[Index].Name, OutputPins[Index].Type);
		bRenamed |= Outputs[Index].OutputName != Name;
		Outputs[Index] = FExpressionOutput(Name);
	}
#endif
	return bRenamed;
}

void UMaterialExpressionVrrBase::PostLoad()
{
	Super::PostLoad();
	RefreshOutputs();
}

#if WITH_EDITOR
void UMaterialExpressionVrrBase::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(Caption);
}

EMaterialValueType UMaterialExpressionVrrBase::GetOutputValueType(int32 OutputIndex)
{
	return OutputPins.IsValidIndex(OutputIndex) ? OutputPins[OutputIndex].Type : Super::GetOutputValueType(OutputIndex);
}

FName UMaterialExpressionVrrBase::GetInputName(int32 InputIndex) const
{
	const TArray<FInputPin>& Pins = InputPinsOf(GetClass());
	return Pins.IsValidIndex(InputIndex) && !Pins[InputIndex].Name.IsNone() ? Pins[InputIndex].Name : Super::GetInputName(InputIndex);
}

EMaterialValueType UMaterialExpressionVrrBase::GetInputValueType(int32 InputIndex)
{
	const TArray<FInputPin>& Pins = InputPinsOf(GetClass());
	return Pins.IsValidIndex(InputIndex) && Pins[InputIndex].Type != MCT_Unknown ? Pins[InputIndex].Type : Super::GetInputValueType(InputIndex);
}

void UMaterialExpressionVrrBase::PostEditImport()
{
	Super::PostEditImport();
	RefreshOutputs();	// pasted nodes
}

void UMaterialExpressionVrrBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (RefreshOutputs() && GraphNode)
	{
		GraphNode->ReconstructNode();	// a property changed an output's type: show the new tag
	}
}

int32 UMaterialExpressionVrrBase::MissingInput(FMaterialCompiler* Compiler, const TCHAR* InputName) const
{
	return Compiler->Errorf(TEXT("%s: missing %s input"), Caption, InputName);
}

int32 UMaterialExpressionVrrBase::NodeError(FMaterialCompiler* Compiler, const TCHAR* Message) const
{
	return Compiler->Errorf(TEXT("%s: %s"), Caption, Message);
}
#endif

#undef LOCTEXT_NAMESPACE
