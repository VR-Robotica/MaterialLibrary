#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialExpression.h"
#include "MF_Base.generated.h"

/**
 * Shared base for every VRR material node (UMaterialExpressionVrr*).
 *
 * Strategy
 *  Each node describes itself once, in its constructor:
 *      InitNode(TEXT("Vrr Dissolve"), { TEXT("Opacity Mask"), TEXT("Edge Emissive"), TEXT("Edge Mask") });
 *  and the base handles what every node used to repeat:
 *   - lists the node under the "VRR" category of the material palette,
 *   - names the output pins and shows the names on them (output index = order in the list),
 *   - returns the caption for the node title,
 *   - formats compile errors with the node's name ("Vrr Dissolve: missing Noise input").
 *  Subclasses only add their inputs, properties and Compile().
 *
 * Abstract, so it never shows up in the palette itself. Changing a node's parent to this class doesn't affect saved
 * materials: they store the node's own class and its properties by name.
 */
UCLASS(Abstract, collapsecategories, hidecategories=Object)
class VRR_MATERIALS_API UMaterialExpressionVrrBase : public UMaterialExpression
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrBase(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#endif

protected:
	/** Sets the node title and its output pins, in output index order. Call from the subclass constructor. */
	void InitNode(const TCHAR* InCaption, std::initializer_list<const TCHAR*> OutputNames);

#if WITH_EDITOR
	/** Compile error for a required input that isn't connected. Return its result from Compile(). */
	int32 MissingInput(class FMaterialCompiler* Compiler, const TCHAR* InputName) const;

	/** Compile error prefixed with the node's name. Return its result from Compile(). */
	int32 NodeError(class FMaterialCompiler* Compiler, const TCHAR* Message) const;
#endif

private:
#if WITH_EDITORONLY_DATA
	/** Node title in the material graph, e.g. "Vrr Dissolve". */
	FString Caption;
#endif
};
