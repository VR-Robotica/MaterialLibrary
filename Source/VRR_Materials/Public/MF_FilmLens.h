#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialExpression.h"
#include "MF_FilmLens.generated.h"

/**
 * Post-process lens imperfections, read from PostProcessInput0 (use in a Post Process material):
 *
 *  - Chromatic aberration: red is sampled pushed outward from the screen center and blue pulled inward,
 *    growing toward the edges: offset = (UV - 0.5) * 2 * Amount * r^Falloff, where r is 0 at the center and 1 in the corners.
 *  - Softness: a small 5-tap blur (center + 4 diagonals at Softness pixels), like a slightly soft film lens.
 *
 * Output: the filtered scene color. Chain Vrr Halation (added) and Vrr Film Grain after it.
 */
UCLASS(collapsecategories, hidecategories=Object)
class VRR_MATERIALS_API UMaterialExpressionVrrFilmLens : public UMaterialExpression
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrFilmLens(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Viewport UV to read the scene at. Defaults to this pixel"))
	FExpressionInput UV;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Chromatic Aberration' if not specified"))
	FExpressionInput ChromaticAberrationIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Softness' if not specified"))
	FExpressionInput SoftnessIn;

	/** Color split in the screen corners, as a fraction of the screen (0.004 = 0.4%, about 4 pixels at 1080p). 0 = off. */
	UPROPERTY(EditAnywhere, Category = "Film Lens", meta = (OverridingInputProperty = "ChromaticAberrationIn", ClampMin = "0.0", UIMax = "0.02"))
	float ChromaticAberration = 0.004f;

	/** How fast the split grows toward the edges: 1 = linear, 2 = mostly in the corners. */
	UPROPERTY(EditAnywhere, Category = "Film Lens", meta = (ClampMin = "0.5", UIMax = "4.0"))
	float ChromaticFalloff = 2.0f;

	/** Blur radius in pixels at 1080p (scales with resolution). 0 = sharp. */
	UPROPERTY(EditAnywhere, Category = "Film Lens", meta = (OverridingInputProperty = "SoftnessIn", ClampMin = "0.0", UIMax = "4.0"))
	float Softness = 0.5f;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#endif
};
