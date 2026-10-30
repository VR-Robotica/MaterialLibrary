// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
#include "MF_TapeSignal.generated.h"

/**
 * Video tape signal look for a Post Process material, applied to Color (e.g. Vrr Film Lens):
 *
 *  - Chroma bleed: tape stores color at a fraction of the brightness resolution, so color smears sideways and lags
 *    to the right while brightness stays sharp. Brightness (Y) comes from Color, color (IQ) from a horizontal blur of
 *    PostProcessInput0, shifted right; Chroma Bleed blends between the two.
 *  - Scanlines: dark gaps between lines, thinner on bright pixels (bright lines spread, as on a CRT).
 *  - Static: fine noise, much heavier inside glitch bands (connect Vrr Tape Warp's Glitch).
 *  - Dropouts: rare white streaks along a line, where the tape lost its signal for a moment.
 *
 * Connect Vrr Tape Warp's UV to UV so the chroma, lines and noise follow the warped picture.
 * Follow it with Vrr Tape Grade for the faded colors of a worn tape.
 */
UCLASS(collapsecategories, hidecategories=Object)
class MATERIALLIBRARY_API UMaterialExpressionVrrTapeSignal : public UMaterialExpressionVrrBase
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrTapeSignal(const FObjectInitializer& ObjectInitializer);

	/** The picture (e.g. Vrr Film Lens). */
	UPROPERTY(meta = (VrrType = "V3", RequiredInput = "true"))
	FExpressionInput Color;

	UPROPERTY(meta = (VrrType = "V2", RequiredInput = "false", ToolTip = "Viewport UV, usually Vrr Tape Warp's UV. Defaults to this pixel"))
	FExpressionInput UV;

	UPROPERTY(meta = (VrrType = "S", RequiredInput = "false", ToolTip = "0-1 glitch band mask (Vrr Tape Warp's Glitch): more static and dropouts inside it"))
	FExpressionInput Glitch;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Chroma Bleed' if not specified"))
	FExpressionInput ChromaBleedIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Scanline Intensity' if not specified"))
	FExpressionInput ScanlineIntensityIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Static' if not specified"))
	FExpressionInput StaticIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Dropouts' if not specified"))
	FExpressionInput DropoutsIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Glitch Dropouts' if not specified"))
	FExpressionInput GlitchDropoutsIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Dropout Intensity' if not specified"))
	FExpressionInput DropoutIntensityIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Dropout Length' if not specified"))
	FExpressionInput DropoutLengthIn;

	/** 0 = Color's own color, 1 = fully smeared tape color. */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Chroma", meta = (OverridingInputProperty = "ChromaBleedIn", ClampMin = "0.0", ClampMax = "1.0"))
	float ChromaBleed = 0.8f;

	/** How wide color smears, in pixels at 1080p. 0 skips the bleed (no extra samples). */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Chroma", meta = (ClampMin = "0.0", UIMax = "32.0"))
	float ChromaBleedWidth = 6.0f;

	/** How far color lags to the right of the brightness, in pixels at 1080p. */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Chroma", meta = (UIMin = "-16.0", UIMax = "16.0"))
	float ChromaShift = 2.0f;

	/** Color strength after the tape: below 1 washes the picture out a little. */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Chroma", meta = (ClampMin = "0.0", UIMax = "2.0"))
	float Saturation = 0.9f;

	/** Lines over the screen height. Match Vrr Tape Warp's Line Count. */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Scanlines", meta = (ClampMin = "16", UIMax = "1080"))
	float ScanlineCount = 240.0f;

	/** How dark the gaps between lines get. 0 = no scanlines. */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Scanlines", meta = (OverridingInputProperty = "ScanlineIntensityIn", ClampMin = "0.0", ClampMax = "1.0"))
	float ScanlineIntensity = 0.25f;

	/** Lines per second the pattern scrolls down. 0 = still. */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Scanlines", meta = (UIMin = "-30.0", UIMax = "30.0"))
	float ScanlineRoll = 0.0f;

	/** Fine noise everywhere: how far it pushes the color. */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Noise", meta = (OverridingInputProperty = "StaticIn", ClampMin = "0.0", UIMax = "0.5"))
	float Static = 0.03f;

	/** Extra noise inside glitch bands. */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Noise", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float GlitchStatic = 0.35f;

	/** Chance per line per frame of a white dropout streak. 0 = none outside glitch bands. */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Dropouts", meta = (OverridingInputProperty = "DropoutsIn", ClampMin = "0.0", ClampMax = "1.0"))
	float Dropouts = 0.004f;

	/** Extra chance per line per frame inside glitch bands. 0 = glitches add no streaks. */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Dropouts", meta = (OverridingInputProperty = "GlitchDropoutsIn", ClampMin = "0.0", ClampMax = "1.0"))
	float GlitchDropouts = 0.3f;

	/** Brightness added along a dropout streak. 0 = no streaks. */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Dropouts", meta = (OverridingInputProperty = "DropoutIntensityIn", ClampMin = "0.0", UIMax = "2.0"))
	float DropoutIntensity = 0.6f;

	/** Longest streak, as a fraction of the screen width (each streak is 20-100% of this). */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Dropouts", meta = (OverridingInputProperty = "DropoutLengthIn", ClampMin = "0.01", UIMax = "1.0"))
	float DropoutLength = 0.3f;

	/** Noise patterns per second (30 for NTSC video). */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Noise", meta = (ClampMin = "1.0", UIMax = "60.0"))
	float FrameRate = 30.0f;

	/** Keep the noise moving while the game is paused (real time). */
	UPROPERTY(EditAnywhere, Category = "Tape Signal|Noise")
	bool bIgnorePause = true;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif
};
