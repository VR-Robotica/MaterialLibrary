// Copyright VR Robotica. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
#include "MF_TapeWarp.generated.h"

/**
 * Video tape picture instability for a Post Process material. Doesn't sample anything: it bends the screen UV,
 * so feed its UV output to every node that reads the scene (Vrr Film Lens, Vrr Halation, Vrr Tape Signal).
 * All offsets are horizontal, in pixels at 1080p, and change at the tape's frame rate.
 *
 *  - Line jitter:    each scanline shifts by a random amount every frame (unsteady tracking).
 *  - Wobble:         a slow horizontal wave rolling down the picture.
 *  - Glitch bands:   in random moments (Glitch Chance per Glitch Rate window), a band rolls down the screen
 *                    and tears its lines sideways. The Glitch output is that band's 0-1 mask.
 *  - Color split:    inside the band, red and blue drift apart in a wave that runs down the lines. The Color Split
 *                    output is that shift in pixels at 1080p: feed it to Vrr Film Lens's Channel Shift.
 *  - Head switching: the bottom lines bend sideways, like the head-switch noise at the bottom of a VHS frame.
 */
UCLASS(collapsecategories, hidecategories=Object)
class MATERIALLIBRARY_API UMaterialExpressionVrrTapeWarp : public UMaterialExpressionVrrBase
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrTapeWarp(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(meta = (VrrType = "V2", RequiredInput = "false", ToolTip = "Viewport UV to warp. Defaults to this pixel"))
	FExpressionInput UV;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Line Jitter' if not specified"))
	FExpressionInput LineJitterIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Glitch Chance' if not specified"))
	FExpressionInput GlitchChanceIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Glitch Strength' if not specified"))
	FExpressionInput GlitchStrengthIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Head Switch Shift' if not specified"))
	FExpressionInput HeadSwitchShiftIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Color Split' if not specified"))
	FExpressionInput ColorSplitIn;

	/** Scanlines in the picture: jitter and tearing move whole lines this tall. NTSC VHS is about 240 per field. */
	UPROPERTY(EditAnywhere, Category = "Tape Warp", meta = (ClampMin = "16", UIMax = "1080"))
	float LineCount = 240.0f;

	/** Picture updates per second (30 for NTSC video, 25 for PAL). */
	UPROPERTY(EditAnywhere, Category = "Tape Warp", meta = (ClampMin = "1.0", UIMax = "60.0"))
	float FrameRate = 30.0f;

	/** Keep the picture moving while the game is paused (real time). */
	UPROPERTY(EditAnywhere, Category = "Tape Warp")
	bool bIgnorePause = true;

	/** Random sideways shift of each line every frame, in pixels at 1080p. */
	UPROPERTY(EditAnywhere, Category = "Tape Warp|Lines", meta = (OverridingInputProperty = "LineJitterIn", ClampMin = "0.0", UIMax = "8.0"))
	float LineJitter = 0.6f;

	/** Size of the slow horizontal wave, in pixels at 1080p. */
	UPROPERTY(EditAnywhere, Category = "Tape Warp|Lines", meta = (ClampMin = "0.0", UIMax = "8.0"))
	float WobbleAmount = 0.5f;

	/** Waves over the screen height. */
	UPROPERTY(EditAnywhere, Category = "Tape Warp|Lines", meta = (ClampMin = "0.0", UIMax = "20.0"))
	float WobbleFrequency = 3.0f;

	/** How fast the wave rolls (radians per second). */
	UPROPERTY(EditAnywhere, Category = "Tape Warp|Lines", meta = (UIMin = "0.0", UIMax = "10.0"))
	float WobbleSpeed = 1.5f;

	/** Chance (0-1) that a glitch band shows up in each window. 0 = never, 1 = always. */
	UPROPERTY(EditAnywhere, Category = "Tape Warp|Glitch", meta = (OverridingInputProperty = "GlitchChanceIn", ClampMin = "0.0", ClampMax = "1.0"))
	float GlitchChance = 0.25f;

	/** Glitch windows per second: 0.5 = every 2 seconds a glitch may happen (with Glitch Chance) and lasts the window. */
	UPROPERTY(EditAnywhere, Category = "Tape Warp|Glitch", meta = (ClampMin = "0.05", UIMax = "4.0"))
	float GlitchRate = 0.5f;

	/** Band height as a fraction of the screen. */
	UPROPERTY(EditAnywhere, Category = "Tape Warp|Glitch", meta = (ClampMin = "0.005", ClampMax = "1.0"))
	float GlitchHeight = 0.08f;

	/** How fast the band rolls down, in screen heights per second. */
	UPROPERTY(EditAnywhere, Category = "Tape Warp|Glitch", meta = (UIMin = "0.0", UIMax = "4.0"))
	float GlitchSpeed = 0.8f;

	/** Largest sideways tear inside the band, in pixels at 1080p. */
	UPROPERTY(EditAnywhere, Category = "Tape Warp|Glitch", meta = (OverridingInputProperty = "GlitchStrengthIn", ClampMin = "0.0", UIMax = "120.0"))
	float GlitchStrength = 24.0f;

	/** Largest red/blue split inside the band, in pixels at 1080p (each channel moves this far, in opposite directions). 0 = off. */
	UPROPERTY(EditAnywhere, Category = "Tape Warp|Color Split", meta = (OverridingInputProperty = "ColorSplitIn", ClampMin = "0.0", UIMax = "32.0"))
	float ColorSplit = 6.0f;

	/** Waves of the split over the screen height (the band shows a few of them). */
	UPROPERTY(EditAnywhere, Category = "Tape Warp|Color Split", meta = (ClampMin = "0.0", UIMax = "200.0"))
	float ColorSplitWaves = 40.0f;

	/** How fast the wave runs down the band, in waves per second. Negative runs it up. */
	UPROPERTY(EditAnywhere, Category = "Tape Warp|Color Split", meta = (UIMin = "-10.0", UIMax = "10.0"))
	float ColorSplitSpeed = 3.0f;

	/** Height of the head-switching strip at the bottom, as a fraction of the screen. 0 = off. */
	UPROPERTY(EditAnywhere, Category = "Tape Warp|Head Switching", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float HeadSwitchHeight = 0.025f;

	/** Sideways bend of the bottom line, in pixels at 1080p. */
	UPROPERTY(EditAnywhere, Category = "Tape Warp|Head Switching", meta = (OverridingInputProperty = "HeadSwitchShiftIn", ClampMin = "0.0", UIMax = "64.0"))
	float HeadSwitchShift = 12.0f;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif
};
