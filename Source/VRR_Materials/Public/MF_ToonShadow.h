#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialExpression.h"
#include "MF_ToonShadow.generated.h"

class UTexture;

/**
 * Cast shadows for toon materials from the fake light.
 * Reads the depth map rendered by AVrr_ToonShadowCaster and returns 1 = lit, 0 = in shadow.
 * Plug the result into Vrr Toon Shading's Shadow input.
 *
 * The four Shadow* inputs describe the depth map's orthographic view and come from MPC_ToonLighting
 * (MF_ToonLit wires them up):
 *   ShadowOrigin  = capture position (xyz), map width in cm (w, 0 = no caster -> fully lit)
 *   ShadowRight   = right axis (xyz), map resolution (w)
 *   ShadowUp      = up axis (xyz)
 *   ShadowForward = direction the capture looks, away from the light (xyz)
 */
UCLASS(collapsecategories, hidecategories=Object)
class VRR_MATERIALS_API UMaterialExpressionVrrToonShadow : public UMaterialExpression
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrToonShadow(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(meta = (RequiredInput = "true"))
	FExpressionInput ShadowOrigin;

	UPROPERTY(meta = (RequiredInput = "true"))
	FExpressionInput ShadowRight;

	UPROPERTY(meta = (RequiredInput = "true"))
	FExpressionInput ShadowUp;

	UPROPERTY(meta = (RequiredInput = "true"))
	FExpressionInput ShadowForward;

	/** World-space normal used to push the sample point off the surface. Defaults to the mesh normal. */
	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "World-space normal for the normal offset. Defaults to the mesh normal"))
	FExpressionInput Normal;

	/** Depth map written by AVrr_ToonShadowCaster (an R32f render target). */
	UPROPERTY(EditAnywhere, Category = "Shadow")
	TObjectPtr<UTexture> ShadowMap;

	/** Constant depth bias in cm. Raise it if lit surfaces show striped "shadow acne". */
	UPROPERTY(EditAnywhere, Category = "Shadow", meta = (ClampMin = "0.0", UIMax = "20.0"))
	float DepthBias = 2.0f;

	/** Extra depth bias in shadow map texels, scales with map size so it works at any coverage. */
	UPROPERTY(EditAnywhere, Category = "Shadow", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float TexelBias = 1.5f;

	/** Pushes the sample point along the normal, in texels. Removes acne on surfaces at grazing angles. */
	UPROPERTY(EditAnywhere, Category = "Shadow", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float NormalOffset = 1.0f;

	/** Blur radius of the shadow edge, in texels. 0 = one hard sample. */
	UPROPERTY(EditAnywhere, Category = "Shadow", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float SoftnessTexels = 1.0f;

	/** Fraction of the map near its border where shadows fade out, so the edge of the covered area isn't visible. */
	UPROPERTY(EditAnywhere, Category = "Shadow", meta = (ClampMin = "0.001", ClampMax = "0.5"))
	float EdgeFade = 0.1f;

	// Lets the material track the shadow map so it gets cooked with it
	virtual UObject* GetReferencedTexture() const override;
	virtual bool CanReferenceTexture() const override { return true; }

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#endif
};
