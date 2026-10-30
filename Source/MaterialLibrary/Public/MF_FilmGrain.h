// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
#include "MF_FilmGrain.generated.h"

/**
 * Film grain for a Post Process material: soft, clumpy value noise (not per-pixel static) that changes in steps
 * at the film's frame rate (24 fps by default), strongest in the midtones like real emulsion.
 *
 *   Grain  = ValueNoise(PixelPos / Size + per-frame offset) * 2 - 1
 *   Weight = lerp(1, 4 * L * (1 - L), MidtoneResponse)      (L = luminance of Color)
 *   Out    = Color + Grain * Amount * Weight
 *
 * Connect Frame (e.g. Vrr Stepped Time's Frame) to drive the steps yourself; otherwise it steps at Frame Rate.
 */
UCLASS(collapsecategories, hidecategories=Object)
class MATERIALLIBRARY_API UMaterialExpressionVrrFilmGrain : public UMaterialExpressionVrrBase
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrFilmGrain(const FObjectInitializer& ObjectInitializer);

	/** The color to add grain to (e.g. Vrr Film Lens + Vrr Halation). */
	UPROPERTY(meta = (VrrType = "V3", RequiredInput = "true"))
	FExpressionInput Color;

	UPROPERTY(meta = (VrrType = "V2", RequiredInput = "false", ToolTip = "Viewport UV, places the grain. Defaults to this pixel"))
	FExpressionInput UV;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Amount' if not specified"))
	FExpressionInput AmountIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Size' if not specified"))
	FExpressionInput SizeIn;

	UPROPERTY(meta = (VrrType = "S", RequiredInput = "false", ToolTip = "Frame number that picks the grain pattern (e.g. Vrr Stepped Time's Frame). Defaults to stepping at 'Frame Rate'"))
	FExpressionInput Frame;

	/** Grain strength: how far a grain pushes the color (0.05 = 5% at mid gray). */
	UPROPERTY(EditAnywhere, Category = "Film Grain", meta = (OverridingInputProperty = "AmountIn", ClampMin = "0.0", UIMax = "0.3"))
	float Amount = 0.05f;

	/** Grain size in pixels at 1080p (scales with resolution). 1 = per pixel, 2-3 = visible clumps. */
	UPROPERTY(EditAnywhere, Category = "Film Grain", meta = (OverridingInputProperty = "SizeIn", ClampMin = "0.25", UIMax = "6.0"))
	float Size = 1.5f;

	/** New grain patterns per second when Frame isn't connected. Film runs at 24. */
	UPROPERTY(EditAnywhere, Category = "Film Grain", meta = (ClampMin = "1.0", UIMax = "60.0"))
	float FrameRate = 24.0f;

	/** 0 = same grain everywhere, 1 = grain only in the midtones (none in pure black or white), like film. */
	UPROPERTY(EditAnywhere, Category = "Film Grain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MidtoneResponse = 0.7f;

	/** Separate grain per color channel (color film) instead of one gray grain. */
	UPROPERTY(EditAnywhere, Category = "Film Grain")
	bool bColored = false;

	/** Keep the grain moving while the game is paused (real time). Only used when Frame isn't connected. */
	UPROPERTY(EditAnywhere, Category = "Film Grain")
	bool bIgnorePause = true;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif

protected:
	/** Grain is one value, or one per channel when Colored. */
	virtual void UpdateOutputTypes() override;
};
