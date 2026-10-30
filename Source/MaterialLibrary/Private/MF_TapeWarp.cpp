// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Vrr Tape Warp: video tape picture instability for a Post Process material.
 *
 * Strategy
 *  This node never reads the scene. It only bends the screen UV sideways, line by line, and every node that reads
 *  the scene (Vrr Film Lens, Vrr Halation, Vrr Tape Signal) takes the warped UV. That way one warp moves the whole
 *  picture consistently, and its masks (Glitch, Color Split) can drive the other nodes' effects in the same bands.
 *  All offsets are horizontal, measured in 1080p pixels, and random values change per tape frame (Frame Rate).
 *
 *  Sideways offset of a line = jitter + wobble + glitch tear + head switching:
 *   - Line jitter:    random per line per frame (unsteady tracking).
 *   - Wobble:         a slow sine wave rolling down the picture.
 *   - Glitch tear:    random per line, only inside the glitch band.
 *   - Head switching: the bottom strip bends sideways, more toward the bottom line.
 *
 *  Glitch band: time is cut into windows (1 / Glitch Rate seconds). A window has a band with Glitch Chance; the
 *  band starts at a random height and rolls down while the window lasts.
 *
 * Outputs
 *  0 UV           the warped viewport UV
 *  1 Glitch       0-1 band mask (1 in its middle, soft edges)
 *  2 Color Split  signed red/blue shift in 1080p pixels inside the band (for Vrr Film Lens's Channel Shift)
 */

#include "MF_TapeWarp.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

namespace VrrTapeWarp
{
	enum EOutput : int32 { UV = 0, Glitch = 1, ColorSplit = 2 };
}

UMaterialExpressionVrrTapeWarp::UMaterialExpressionVrrTapeWarp(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Tape Warp"), {
		{ TEXT("UV"), MCT_Float2 },
		{ TEXT("Glitch"), MCT_Float1 },
		{ TEXT("Color Split"), MCT_Float1 }
	});
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrTapeWarp::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	const int32 Zero = Compiler->Constant(0.0f);
	const int32 One = Compiler->Constant(1.0f);

	// 1. Shared values: this pixel's height, the clock, a seed per tape frame and the scanline index
	const int32 BaseUV = ViewportUV(Compiler, UV);
	const int32 V = Y(Compiler, BaseUV);
	const int32 Time = Seconds(Compiler, bIgnorePause);
	const int32 Seed = FrameSeed(Compiler, SteppedFrame(Compiler, FrameRate, bIgnorePause));
	const int32 Line = Compiler->Floor(Compiler->Mul(V, Compiler->Constant(FMath::Max(LineCount, 1.0f))));

	// 2. Glitch band
	const float SafeGlitchRate = FMath::Max(GlitchRate, 0.05f);
	const int32 Window = Compiler->Mul(Time, Compiler->Constant(SafeGlitchRate));
	const int32 WindowIndex = Compiler->Floor(Window);
	// 2a. Does this window have a band? (one random roll per window)
	const int32 Active = Compiler->Step(Hash2(Compiler, WindowIndex, Zero, 7.13f), InputOr(Compiler, GlitchChanceIn, GlitchChance));
	// 2b. Where is it: a random start height, rolling down by Glitch Speed for the seconds spent in this window
	const int32 WindowSeconds = Compiler->Div(Compiler->Frac(Window), Compiler->Constant(SafeGlitchRate));
	const int32 BandCenter = Compiler->Frac(Compiler->Add(Hash2(Compiler, WindowIndex, Zero, 3.71f),
		Compiler->Mul(WindowSeconds, Compiler->Constant(GlitchSpeed))));
	// 2c. Mask: 1 in the band's middle, 0 at its edge, squared for a soft falloff
	const int32 BandDistance = Compiler->Div(Compiler->Abs(Compiler->Sub(V, BandCenter)), Compiler->Constant(FMath::Max(GlitchHeight, 0.005f) * 0.5f));
	const int32 BandEdge = Compiler->Saturate(Compiler->Sub(One, BandDistance));
	const int32 Glitch = Compiler->Mul(Active, Compiler->Mul(BandEdge, BandEdge));
	if (OutputIndex == VrrTapeWarp::Glitch)
	{
		return Glitch;
	}

	// 3. Color split: a sine running down the lines, at full size in the band's middle
	if (OutputIndex == VrrTapeWarp::ColorSplit)
	{
		const int32 Phase = Compiler->Mul(Compiler->Sub(Compiler->Mul(V, Compiler->Constant(ColorSplitWaves)), Compiler->Mul(Time, Compiler->Constant(ColorSplitSpeed))), Compiler->Constant(UE_TWO_PI));
		return Compiler->Mul(Compiler->Mul(Compiler->Sine(Phase), InputOr(Compiler, ColorSplitIn, ColorSplit)), Glitch);
	}

	// 4. Sideways offset of this line, in 1080p pixels
	// 4a. Jitter
	int32 Offset = Compiler->Mul(ToSigned(Compiler, Hash2(Compiler, Line, Seed, 0.0f)), InputOr(Compiler, LineJitterIn, LineJitter));
	// 4b. Wobble
	if (WobbleAmount > 0.0f)
	{
		const int32 Phase = Compiler->Add(Compiler->Mul(V, Compiler->Constant(WobbleFrequency * UE_TWO_PI)), Compiler->Mul(Time, Compiler->Constant(WobbleSpeed)));
		Offset = Compiler->Add(Offset, Compiler->Mul(Compiler->Sine(Phase), Compiler->Constant(WobbleAmount)));
	}
	// 4c. Glitch tear
	const int32 Tear = Compiler->Mul(ToSigned(Compiler, Hash2(Compiler, Line, Seed, 13.7f)), InputOr(Compiler, GlitchStrengthIn, GlitchStrength));
	Offset = Compiler->Add(Offset, Compiler->Mul(Tear, Glitch));
	// 4d. Head switching: Strip is 0 above the strip and 1 on the bottom line (viewport V grows downward)
	if (HeadSwitchHeight > 0.0f)
	{
		const int32 Strip = Compiler->Saturate(Compiler->Div(Compiler->Sub(V, Compiler->Constant(1.0f - HeadSwitchHeight)), Compiler->Constant(HeadSwitchHeight)));
		const int32 Wiggle = Compiler->Add(Compiler->Constant(0.6f), Compiler->Mul(Hash2(Compiler, Line, Seed, 5.3f), Compiler->Constant(0.4f)));
		Offset = Compiler->Add(Offset, Compiler->Mul(Compiler->Mul(Compiler->Mul(Strip, Strip), Wiggle), InputOr(Compiler, HeadSwitchShiftIn, HeadSwitchShift)));
	}

	// 5. Pixels -> UV, horizontal only
	const int32 OffsetUV = Compiler->AppendVector(Compiler->Mul(Offset, X(Compiler, PixelAt1080(Compiler))), Zero);
	return Compiler->Add(BaseUV, OffsetUV);
}
#endif
