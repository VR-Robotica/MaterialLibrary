#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
#include "MF_Halation.generated.h"

/**
 * Film halation: on film, bright light passes through the emulsion, reflects off the film base and re-exposes the
 * red layer, leaving a warm red-orange halo around highlights. Reads PostProcessInput0 (use in a Post Process material).
 *
 *   Bright(c) = saturate((Luminance(c) - Threshold) / Knee)
 *   Glow      = Tint * Intensity * weighted average of Bright() over a disk of Samples taps, Radius pixels wide
 *
 * With "Keep Highlights Clean" the glow fades out on the bright pixels themselves, so the light source keeps its own
 * color and the halo surrounds it. Add the Glow output to the scene color (e.g. Vrr Film Lens + Glow).
 */
UCLASS(collapsecategories, hidecategories=Object)
class MATERIALLIBRARY_API UMaterialExpressionVrrHalation : public UMaterialExpressionVrrBase
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrHalation(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(meta = (VrrType = "V2", RequiredInput = "false", ToolTip = "Viewport UV to read the scene at. Defaults to this pixel"))
	FExpressionInput UV;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Threshold' if not specified"))
	FExpressionInput ThresholdIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Radius' if not specified"))
	FExpressionInput RadiusIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Intensity' if not specified"))
	FExpressionInput IntensityIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Tint' if not specified"))
	FExpressionInput TintIn;

	/** Luminance where highlights start to glow. After tonemapping the scene is 0-1, so 0.6-0.8; before tonemapping use 1 or more. */
	UPROPERTY(EditAnywhere, Category = "Halation", meta = (OverridingInputProperty = "ThresholdIn", ClampMin = "0.0"))
	float Threshold = 0.7f;

	/** Luminance range over which the glow fades in above the threshold. */
	UPROPERTY(EditAnywhere, Category = "Halation", meta = (ClampMin = "0.001"))
	float Knee = 0.2f;

	/** Halo radius in pixels at 1080p (scales with resolution). */
	UPROPERTY(EditAnywhere, Category = "Halation", meta = (OverridingInputProperty = "RadiusIn", ClampMin = "0.0", UIMax = "64.0"))
	float Radius = 12.0f;

	UPROPERTY(EditAnywhere, Category = "Halation", meta = (OverridingInputProperty = "IntensityIn", ClampMin = "0.0", UIMax = "4.0"))
	float Intensity = 0.6f;

	/** Halo color. Film halation is red-orange. */
	UPROPERTY(EditAnywhere, Category = "Halation", meta = (OverridingInputProperty = "TintIn"))
	FLinearColor Tint = FLinearColor(1.0f, 0.25f, 0.08f);

	/** Taps on the disk. More is smoother; cost grows with it. */
	UPROPERTY(EditAnywhere, Category = "Halation", meta = (ClampMin = "4", ClampMax = "64"))
	int32 Samples = 24;

	/** Rotate the tap pattern per pixel: turns banding at large radii into fine noise (film grain hides it). */
	UPROPERTY(EditAnywhere, Category = "Halation")
	bool bJitter = true;

	/** Fade the glow out on the highlights themselves, so the halo surrounds bright areas without tinting them. */
	UPROPERTY(EditAnywhere, Category = "Halation")
	bool bKeepHighlightsClean = true;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif
};
