/*
 * Shared setup for the VRR material nodes: palette category, output pins, caption and error messages.
 * See MF_Base.h for why the nodes share a base.
 */

#include "MF_Base.h"

#include "MaterialCompiler.h"

#define LOCTEXT_NAMESPACE "VrrMaterialExpressions"

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
void UMaterialExpressionVrrBase::InitNode(const TCHAR* InCaption, std::initializer_list<const TCHAR*> OutputNames)
{
#if WITH_EDITORONLY_DATA
	Caption = InCaption;

	bShowOutputNameOnPin = true;
	Outputs.Reset();
	for (const TCHAR* Name : OutputNames)
	{
		Outputs.Add(FExpressionOutput(Name));
	}
#endif
}

#if WITH_EDITOR
void UMaterialExpressionVrrBase::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(Caption);
}

int32 UMaterialExpressionVrrBase::MissingInput(FMaterialCompiler* Compiler, const TCHAR* InputName) const
{
	return Compiler->Errorf(TEXT("%s: missing %s input"), *Caption, InputName);
}

int32 UMaterialExpressionVrrBase::NodeError(FMaterialCompiler* Compiler, const TCHAR* Message) const
{
	return Compiler->Errorf(TEXT("%s: %s"), *Caption, Message);
}
#endif

#undef LOCTEXT_NAMESPACE
