#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
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
 *
 * Distortion (cm) makes the shadow edges ripple slightly with world-space noise, like Vrr Blob Shadow's edge.
 * Feed Time In the same stepped boil clock as the outlines (MF_ToonLit does) so they move together.
 */
UCLASS(collapsecategories, hidecategories=Object)
class VRR_MATERIALS_API UMaterialExpressionVrrToonShadow : public UMaterialExpressionVrrBase
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

	/** Clock for the distortion, e.g. Vrr Stepped Time so it steps like the outline boil. Defaults to game time. */
	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Clock for the distortion, e.g. Vrr Stepped Time so it steps like the outline. Defaults to game time"))
	FExpressionInput TimeIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Distortion' if not specified"))
	FExpressionInput DistortionIn;

	/** Depth map written by AVrr_ToonShadowCaster (an R32f render target). */
	UPROPERTY(EditAnywhere, Category = "Shadow")
	TObjectPtr<UTexture> ShadowMap;

	/** Constant depth bias in cm. Raise it if lit surfaces show striped "shadow acne". */
	UPROPERTY(EditAnywhere, Category = "Shadow", meta = (ClampMin = "0.0", UIMax = "20.0"))
	float DepthBias = 2.0f;

	/** Extra depth bias in shadow map texels, scales with map size so it works at any coverage. */
	UPROPERTY(EditAnywhere, Category = "Shadow", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float TexelBias = 1.5f;

	/**
	 * Extra bias on surfaces that turn away from the light, in texels per unit of slope (tan of the angle to the
	 * light), also covering the Softness taps. Removes the diamond-pattern acne on curved surfaces while keeping
	 * contact shadows tight where surfaces face the light. 0 = off.
	 */
	UPROPERTY(EditAnywhere, Category = "Shadow", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float SlopeBias = 1.0f;

	/** Pushes the sample point along the normal, in texels. Removes acne on surfaces at grazing angles. */
	UPROPERTY(EditAnywhere, Category = "Shadow", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float NormalOffset = 1.0f;

	/** Blur radius of the shadow edge, in texels. 0 = one hard sample. */
	UPROPERTY(EditAnywhere, Category = "Shadow", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float SoftnessTexels = 1.0f;

	/** Fraction of the map near its border where shadows fade out, so the edge of the covered area isn't visible. */
	UPROPERTY(EditAnywhere, Category = "Shadow", meta = (ClampMin = "0.001", ClampMax = "0.5"))
	float EdgeFade = 0.1f;

	/**
	 * How far shadow edges wobble, in cm: the map is read at a point pushed around by smooth noise, so edges ripple
	 * like a hand-drawn shadow (like Vrr Blob Shadow's edge). 0 = straight edges (compiled out if not wired).
	 */
	UPROPERTY(EditAnywhere, Category = "Distortion", meta = (OverridingInputProperty = "DistortionIn", ClampMin = "0.0", UIMax = "20.0"))
	float Distortion = 0.0f;

	/** Wobbles per meter across the shadow. Higher = smaller, busier ripples. */
	UPROPERTY(EditAnywhere, Category = "Distortion", meta = (ClampMin = "0.0", UIMax = "10.0"))
	float DistortionScale = 1.5f;

	/** How fast the wobble pattern moves per unit of Time. 0 = static. */
	UPROPERTY(EditAnywhere, Category = "Distortion", meta = (ClampMin = "0.0", UIMax = "8.0"))
	float DistortionSpeed = 2.0f;

	// Lets the material track the shadow map so it gets cooked with it
	virtual UObject* GetReferencedTexture() const override;
	virtual bool CanReferenceTexture() const override { return true; }

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif
};
