// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialExpression.h"
#include "MF_Base.generated.h"

/** One output pin of a VRR node: its name (without type tag) and the type Compile() returns for it. */
struct FVrrOutput
{
	const TCHAR* Name;	// a TEXT() literal
	EMaterialValueType Type;
};

/**
 * Shared base for every VRR material node (UMaterialExpressionVrr*).
 *
 * Strategy
 *  Each node describes itself once, in its constructor:
 *      InitNode(TEXT("Vrr Dissolve"), { { TEXT("Opacity Mask"), MCT_Float1 }, { TEXT("Edge Emissive"), MCT_Float3 }, ... });
 *  and the base handles what every node used to repeat:
 *   - lists the node under the "VRR" category of the material palette,
 *   - names the output pins and shows the names on them (output index = order in the list), each with its type tag:
 *     "Opacity Mask (S)", "Edge Emissive (V3)" (S, V2, V3, V4), so the type shows on the node and the named reroute
 *     tools can prefix by it,
 *   - reports each output's type to the editor (GetOutputValueType), for wire checks and the reroute tools,
 *   - names input pins the same way, from the property: a trailing "In" is dropped and the tag added
 *     ("BaseColorIn" -> "Base Color (V3)"). The input's type comes from the constant that stands in for it when it's
 *     unwired (the property with OverridingInputProperty: color / vector -> V3, float / int -> S, bool -> SB), else
 *     from the input's own VrrType metadata: UPROPERTY(meta = (VrrType = "V2")) FExpressionInput UV;
 *     Worked out once per node class.
 *   - returns the caption for the node title,
 *   - formats compile errors with the node's name ("Vrr Dissolve: missing Noise input").
 *  Subclasses only add their inputs, properties and Compile().
 *
 * Output names are saved with each node, so they are rebuilt on load: renaming a pin here updates saved materials
 * (links go by output index, never by name). A node whose output type depends on a property (Film Grain's Colored)
 * overrides UpdateOutputTypes() and calls SetOutputType(); the pins are refreshed when that property changes.
 *
 * Abstract, so it never shows up in the palette itself. Changing a node's parent to this class doesn't affect saved
 * materials: they store the node's own class and its properties by name.
 */
UCLASS(Abstract, collapsecategories, hidecategories=Object)
class MATERIALLIBRARY_API UMaterialExpressionVrrBase : public UMaterialExpression
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrBase(const FObjectInitializer& ObjectInitializer);

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual EMaterialValueType GetOutputValueType(int32 OutputIndex) override;
	virtual FName GetInputName(int32 InputIndex) const override;
	virtual EMaterialValueType GetInputValueType(int32 InputIndex) override;
	virtual void PostEditImport() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** The type tag shown after a pin name, as the engine tags function inputs: "S", "V2", "V3", "V4", "SB", "B", or empty. */
	static const TCHAR* TypeTag(EMaterialValueType Type);

protected:
	/** Sets the node title and its output pins, in output index order. Call from the subclass constructor. */
	void InitNode(const TCHAR* InCaption, std::initializer_list<FVrrOutput> InOutputs);

	/** For outputs whose type depends on properties: call SetOutputType() for them. Runs after load, paste and edits. */
	virtual void UpdateOutputTypes() {}

	/** Changes an output's type (and so its tag). */
	void SetOutputType(int32 OutputIndex, EMaterialValueType Type);

#if WITH_EDITOR
	/** Compile error for a required input that isn't connected. Return its result from Compile(). */
	int32 MissingInput(class FMaterialCompiler* Compiler, const TCHAR* InputName) const;

	/** Compile error prefixed with the node's name. Return its result from Compile(). */
	int32 NodeError(class FMaterialCompiler* Compiler, const TCHAR* Message) const;
#endif

private:
	/** Refreshes types, then rebuilds Outputs from OutputPins. True if any pin name changed. */
	bool RefreshOutputs();

#if WITH_EDITORONLY_DATA
	/** Node title in the material graph, e.g. "Vrr Dissolve" (a TEXT() literal). */
	const TCHAR* Caption = TEXT("");

	/** The output pins by output index, set by the constructor (types may change, see UpdateOutputTypes). */
	TArray<FVrrOutput> OutputPins;
#endif
};
