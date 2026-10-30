// Copyright VR Robotica. All Rights Reserved.

/*
 * Vrr Tape Grade: the color of a worn 1990s video tape (levels, faded color, hot reds, tints, hue drift) for a Post
 * Process material placed after tonemapping.
 *
 * Strategy
 *  Works in YIQ, the color space NTSC tape records: brightness (Y) and color (IQ) travel separately on tape and wear
 *  out differently, so each is graded on its own:
 *   - Brightness goes through a camcorder-style curve: a soft knee rolls off highlights, a gamma washes out the
 *     midtones, then the result is squeezed into the tape's levels (lifted black, dim white).
 *   - Color fades overall and drains out of shadows and highlights (where tape noise and clipping eat it), while
 *     reds keep extra saturation. Tints push shadows teal and highlights warm, then a hue rotation (fixed shift plus
 *     a slow wobble rolling down the screen) copies the tape's color phase errors.
 *  The zones (shadow / highlight) are measured on the incoming brightness, so the grade is stable regardless of the
 *  level settings. Every setting blends from its "clean" value to the property by Wear, so Wear 0 is a pass-through.
 *
 * Steps
 *  1. Brightness curve: knee, gamma, levels.
 *  2. Color fade: overall, shadows, highlights; red boost.
 *  3. Tints: teal shadows, warm highlights.
 *  4. Hue rotation: Hue Shift + Hue Drift wobble.
 *
 * Output
 *  0 Color  the graded picture (never negative)
 */

#include "MF_TapeGrade.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

namespace VrrTapeGrade
{
	/** Hue that Red Boost targets: a slightly orange red, where tape reds bleed. */
	const FLinearColor BoostHue(1.0f, 0.2f, 0.05f);

	/** Red Boost reaches colors within about 60 degrees of BoostHue (cosine of the angle above this). */
	constexpr float BoostCosine = 0.5f;
}

UMaterialExpressionVrrTapeGrade::UMaterialExpressionVrrTapeGrade(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Tape Grade"), { { TEXT("Color"), MCT_Float3 } });
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrTapeGrade::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;
	using namespace VrrTapeGrade;

	if (!IsConnected(Color))
	{
		return MissingInput(Compiler, TEXT("Color"));
	}
	const int32 Zero = Compiler->Constant(0.0f);
	const int32 One = Compiler->Constant(1.0f);
	const int32 ColorArg = Compiler->Max(Compiler->ValidCast(Color.Compile(Compiler), MCT_Float3), Zero);
	const int32 WearArg = Compiler->Max(InputOr(Compiler, WearIn, Wear), Zero);

	// A setting blended by Wear: its clean value at Wear 0, Value at Wear 1
	auto Worn = [&](float Clean, int32 Value) { return Compiler->Lerp(Compiler->Constant(Clean), Value, WearArg); };
	auto WornF = [&](float Clean, float Value) { return Worn(Clean, Compiler->Constant(Value)); };

	const int32 ColorYIQ = ToYIQ(Compiler, ColorArg);
	const int32 LumaIn = Compiler->Max(X(Compiler, ColorYIQ), Zero);
	const int32 LumaZone = Compiler->Saturate(LumaIn);
	int32 Chroma = IQ(Compiler, ColorYIQ);

	// 1. Brightness curve
	int32 Luma;
	{
		// 1a. Knee: above K, the overshoot x = (Y - K) / (1 - K) rolls off as x / (1 + x), so it never passes 1
		const int32 K = WornF(1.0f, Knee);
		const int32 Range = Compiler->Max(Compiler->Sub(One, K), Compiler->Constant(0.001f));
		const int32 Over = Compiler->Div(Compiler->Max(Compiler->Sub(LumaIn, K), Zero), Range);
		Luma = Compiler->Add(Compiler->Min(LumaIn, K), Compiler->Mul(Range, Compiler->Div(Over, Compiler->Add(One, Over))));

		// 1b. Gamma on the 0-1 curve
		Luma = Compiler->Power(Compiler->Max(Luma, Compiler->Constant(0.00001f)), Compiler->Max(WornF(1.0f, Gamma), Compiler->Constant(0.05f)));

		// 1c. Levels: 0-1 squeezed into Black..White
		const int32 Black = Worn(0.0f, InputOr(Compiler, BlackLevelIn, BlackLevel));
		const int32 White = WornF(1.0f, WhiteLevel);
		Luma = Compiler->Add(Black, Compiler->Mul(Luma, Compiler->Sub(White, Black)));
	}

	// 2. Color fade, then extra saturation on reds
	{
		const int32 ShadowZone = Compiler->Sub(One, Compiler->SmoothStep(Zero, Compiler->Constant(0.35f), LumaZone));
		const int32 HighlightZone = Compiler->SmoothStep(Compiler->Constant(0.65f), One, LumaZone);
		const int32 KeepShadow = Compiler->Saturate(Compiler->Sub(One, Compiler->Mul(WornF(0.0f, ShadowFade), ShadowZone)));
		const int32 KeepHighlight = Compiler->Saturate(Compiler->Sub(One, Compiler->Mul(WornF(0.0f, HighlightFade), HighlightZone)));
		int32 Amount = Compiler->Max(Worn(1.0f, InputOr(Compiler, SaturationIn, Saturation)), Zero);
		Amount = Compiler->Mul(Amount, Compiler->Mul(KeepShadow, KeepHighlight));

		if (RedBoost > 0.0f)
		{
			// Cosine between this pixel's hue and the boost hue, remapped so only nearby hues count (squared for a soft edge)
			const FVector3f Boost = ToYIQ(BoostHue);
			const FVector2f BoostDir = FVector2f(Boost.Y, Boost.Z).GetSafeNormal();
			const int32 Cos = Compiler->Div(Compiler->Dot(Chroma, Compiler->Constant2(BoostDir.X, BoostDir.Y)), Compiler->Add(Compiler->Length(Chroma), Compiler->Constant(0.0001f)));
			const int32 Near = Compiler->Saturate(Compiler->Div(Compiler->Sub(Cos, Compiler->Constant(BoostCosine)), Compiler->Constant(1.0f - BoostCosine)));
			Amount = Compiler->Mul(Amount, Compiler->Add(One, Compiler->Mul(WornF(0.0f, RedBoost), Compiler->Mul(Near, Near))));
		}
		Chroma = Compiler->Mul(Chroma, Amount);
	}

	// 3. Tints: only their color (IQ) is added, so they shift hue without changing brightness
	if (TintStrength > 0.0f)
	{
		const FVector3f Shadow = ToYIQ(ShadowTint);
		const FVector3f Highlight = ToYIQ(HighlightTint);
		const int32 ShadowZone = Compiler->Sub(One, Compiler->SmoothStep(Zero, Compiler->Constant(0.6f), LumaZone));
		const int32 HighlightZone = Compiler->SmoothStep(Compiler->Constant(0.4f), One, LumaZone);
		const int32 Tint = Compiler->Add(
			Compiler->Mul(Compiler->Constant2(Shadow.Y, Shadow.Z), ShadowZone),
			Compiler->Mul(Compiler->Constant2(Highlight.Y, Highlight.Z), HighlightZone));
		Chroma = Compiler->Add(Chroma, Compiler->Mul(Tint, WornF(0.0f, TintStrength)));
	}

	// 4. Hue rotation: rotate IQ by Hue Shift plus a -1..1 wobble from value noise over (screen height, time)
	const bool bDrift = IsConnected(HueDriftIn) || HueDrift > 0.0f;
	if (bDrift || HueShift != 0.0f)
	{
		int32 Degrees = WornF(0.0f, HueShift);
		if (bDrift)
		{
			const int32 V = Y(Compiler, ViewportUV(Compiler, UV));
			const int32 NoisePos = Compiler->AppendVector(
				Compiler->Mul(V, Compiler->Constant(HueDriftBands)),
				Compiler->Mul(Seconds(Compiler, bIgnorePause), Compiler->Constant(HueDriftSpeed)));
			const int32 Wobble = ToSigned(Compiler, ValueNoise(Compiler, NoisePos));
			Degrees = Compiler->Add(Degrees, Compiler->Mul(Wobble, Worn(0.0f, InputOr(Compiler, HueDriftIn, HueDrift))));
		}
		const int32 Radians = Compiler->Mul(Degrees, Compiler->Constant(UE_PI / 180.0f));
		Chroma = Rotate2D(Compiler, X(Compiler, Chroma), Y(Compiler, Chroma), Compiler->Cosine(Radians), Compiler->Sine(Radians));
	}

	return Compiler->Max(YIQToRGB(Compiler, Compiler->AppendVector(Luma, Chroma)), Zero);
}
#endif
