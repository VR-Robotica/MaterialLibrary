/*
 * Vrr Tape Signal: the video tape signal look (chroma bleed, scanlines, static, dropouts) for a Post Process material.
 *
 * Strategy
 *  Each effect copies one physical flaw of analog video and is applied in the order the signal would suffer it:
 *   1. Chroma bleed: tape records color (IQ in NTSC's YIQ space) at a fraction of the brightness (Y) resolution, so
 *      color smears sideways and lags to the right while brightness stays sharp. Y is kept from Color; IQ is
 *      replaced by a horizontal blur of the scene, shifted right.
 *   2. Scanlines: a cosine per line darkens the gaps between lines. Bright lines spread on a CRT, so the gaps
 *      shrink on bright pixels.
 *   3. Static: a random value per 1080p pixel per frame, much heavier inside glitch bands.
 *   4. Dropouts: rare white streaks along a line where the tape lost its signal, fading over their length.
 *  All randomness is per tape frame (Frame Rate), so the noise flickers at video rate, not at render rate.
 *  Connect Vrr Tape Warp's UV so every effect follows the warped picture, and its Glitch so they intensify in bands.
 *
 * Output
 *  0 Color  the picture after the tape (never negative)
 */

#include "MF_TapeSignal.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

namespace VrrTapeSignal
{
	/** Scene taps averaged for the chroma smear. */
	constexpr int32 ChromaTaps = 5;

	/** RGB -> YIQ (NTSC): Y is brightness, I and Q are color. */
	int32 ToYIQ(FMaterialCompiler* Compiler, int32 RGB)
	{
		const int32 Y = Compiler->Dot(RGB, Compiler->Constant3(0.299f, 0.587f, 0.114f));
		const int32 I = Compiler->Dot(RGB, Compiler->Constant3(0.596f, -0.274f, -0.322f));
		const int32 Q = Compiler->Dot(RGB, Compiler->Constant3(0.211f, -0.523f, 0.312f));
		return Compiler->AppendVector(Compiler->AppendVector(Y, I), Q);
	}

	/** YIQ -> RGB (inverse of ToYIQ). */
	int32 ToRGB(FMaterialCompiler* Compiler, int32 YIQ)
	{
		const int32 R = Compiler->Dot(YIQ, Compiler->Constant3(1.0f, 0.956f, 0.621f));
		const int32 G = Compiler->Dot(YIQ, Compiler->Constant3(1.0f, -0.272f, -0.647f));
		const int32 B = Compiler->Dot(YIQ, Compiler->Constant3(1.0f, -1.106f, 1.703f));
		return Compiler->AppendVector(Compiler->AppendVector(R, G), B);
	}

	/** The IQ (color) part of a YIQ value. */
	int32 IQ(FMaterialCompiler* Compiler, int32 YIQ)
	{
		return Compiler->ComponentMask(YIQ, false, true, true, false);
	}
}

UMaterialExpressionVrrTapeSignal::UMaterialExpressionVrrTapeSignal(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Tape Signal"), { TEXT("Color") });
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrTapeSignal::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;
	using namespace VrrTapeSignal;

	if (!IsConnected(Color))
	{
		return MissingInput(Compiler, TEXT("Color"));
	}
	const int32 Zero = Compiler->Constant(0.0f);
	const int32 One = Compiler->Constant(1.0f);
	const int32 ColorArg = Compiler->ValidCast(Color.Compile(Compiler), MCT_Float3);
	const int32 BaseUV = ViewportUV(Compiler, UV);
	const int32 Pixel = PixelAt1080(Compiler);
	const int32 GlitchArg = IsConnected(Glitch) ? Compiler->ValidCast(Glitch.Compile(Compiler), MCT_Float1) : Zero;

	// 1. Chroma bleed: Y from Color, IQ from scene taps spread over the bleed width, centered ChromaShift to the right
	const int32 ColorYIQ = ToYIQ(Compiler, ColorArg);
	const int32 Luma = X(Compiler, ColorYIQ);
	int32 Chroma = IQ(Compiler, ColorYIQ);
	if (ChromaBleedWidth > 0.0f)
	{
		int32 Sum = Compiler->Constant2(0.0f, 0.0f);
		for (int32 Tap = 0; Tap < ChromaTaps; ++Tap)
		{
			// Evenly spaced from -Width/2 to +Width/2 around the shift
			const float OffsetPx = ChromaShift + ChromaBleedWidth * (static_cast<float>(Tap) / (ChromaTaps - 1) - 0.5f);
			const int32 TapUV = Compiler->Add(BaseUV, Compiler->Mul(Pixel, Compiler->Constant2(OffsetPx, 0.0f)));
			Sum = Compiler->Add(Sum, IQ(Compiler, ToYIQ(Compiler, SampleScene(Compiler, TapUV))));
		}
		const int32 Smeared = Compiler->Mul(Sum, Compiler->Constant(1.0f / ChromaTaps));
		Chroma = Compiler->Lerp(Chroma, Smeared, InputOr(Compiler, ChromaBleedIn, ChromaBleed));
	}
	Chroma = Compiler->Mul(Chroma, Compiler->Constant(Saturation));
	int32 Result = ToRGB(Compiler, Compiler->AppendVector(Luma, Chroma));

	// 2. Scanlines: Wave is 1 on a line's center and 0 in the gap between lines
	const int32 V = Y(Compiler, BaseUV);
	{
		int32 LinePos = Compiler->Mul(V, Compiler->Constant(ScanlineCount));
		if (ScanlineRoll != 0.0f)
		{
			LinePos = Compiler->Add(LinePos, Compiler->Mul(Seconds(Compiler, bIgnorePause), Compiler->Constant(ScanlineRoll)));
		}
		const int32 Wave = Compiler->Add(Compiler->Constant(0.5f), Compiler->Mul(Compiler->Constant(0.5f), Compiler->Cosine(Compiler->Mul(LinePos, Compiler->Constant(UE_TWO_PI)))));
		// Gap depth: full Intensity on black, half on white (bright lines spread into the gaps)
		const int32 Depth = Compiler->Mul(InputOr(Compiler, ScanlineIntensityIn, ScanlineIntensity), Compiler->Sub(One, Compiler->Mul(Compiler->Saturate(Luma), Compiler->Constant(0.5f))));
		Result = Compiler->Mul(Result, Compiler->Sub(One, Compiler->Mul(Depth, Compiler->Sub(One, Wave))));
	}

	// 3. Static: a random -0.5..0.5 value per 1080p pixel per tape frame, heavier in glitch bands
	const int32 Seed = FrameSeed(Compiler, SteppedFrame(Compiler, FrameRate, bIgnorePause));
	{
		const int32 Cell = Compiler->Floor(Compiler->Div(BaseUV, Pixel));
		const int32 Noise = Compiler->Sub(Hash12(Compiler, Compiler->Add(Cell, Seed)), Compiler->Constant(0.5f));
		const int32 Amount = Compiler->Add(InputOr(Compiler, StaticIn, Static), Compiler->Mul(GlitchArg, Compiler->Constant(GlitchStatic)));
		Result = Compiler->Add(Result, Compiler->Mul(Noise, Compiler->Mul(Amount, Compiler->Constant(2.0f))));
	}

	// 4. Dropouts, skipped entirely when they can't show (no rate and nothing wired)
	const bool bHasDropoutInputs = IsConnected(DropoutsIn) || IsConnected(GlitchDropoutsIn) || IsConnected(DropoutIntensityIn);
	const bool bGlitchDropouts = IsConnected(Glitch) && (GlitchDropouts > 0.0f || IsConnected(GlitchDropoutsIn));
	if (bHasDropoutInputs || ((Dropouts > 0.0f || bGlitchDropouts) && DropoutIntensity > 0.0f))
	{
		// 4a. Does this line have a streak this frame? (chance = Dropouts, plus Glitch Dropouts inside a band)
		const int32 Line = Compiler->Floor(Compiler->Mul(V, Compiler->Constant(ScanlineCount)));
		const int32 Rate = Compiler->Add(InputOr(Compiler, DropoutsIn, Dropouts), Compiler->Mul(GlitchArg, InputOr(Compiler, GlitchDropoutsIn, GlitchDropouts)));
		const int32 On = Compiler->Step(Hash2(Compiler, Line, Seed, 3.1f), Rate);

		// 4b. Random start and length (20-100% of Dropout Length) per line per frame
		const int32 Start = Hash2(Compiler, Line, Seed, 8.7f);
		const int32 MaxLength = Compiler->Max(InputOr(Compiler, DropoutLengthIn, DropoutLength), Compiler->Constant(0.01f));
		const int32 Length = Compiler->Mul(MaxLength, Compiler->Add(Compiler->Constant(0.2f), Compiler->Mul(Hash2(Compiler, Line, Seed, 1.9f), Compiler->Constant(0.8f))));

		// 4c. Along = 0 at the start, 1 at the end. Bright at the start, fading to nothing at the end.
		const int32 Along = Compiler->Div(Compiler->Sub(X(Compiler, BaseUV), Start), Length);
		const int32 Inside = Compiler->Mul(Compiler->Step(Zero, Along), Compiler->Step(Along, One));
		const int32 Streak = Compiler->Mul(Compiler->Mul(On, Inside), Compiler->Sub(One, Compiler->Saturate(Along)));
		Result = Compiler->Add(Result, Compiler->Mul(Streak, InputOr(Compiler, DropoutIntensityIn, DropoutIntensity)));
	}

	// Static can push dark pixels below zero
	return Compiler->Max(Result, Zero);
}
#endif
