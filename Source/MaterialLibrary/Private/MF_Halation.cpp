// Copyright VR Robotica. All Rights Reserved.

/*
 * Vrr Halation: the warm red-orange halo film gets around bright highlights. Use in a Post Process material.
 *
 * Strategy
 *  On film, bright light passes through the emulsion, reflects off the film base and re-exposes the red layer
 *  around the highlight. To fake that, each pixel looks at its neighborhood and asks "how much bright light is
 *  around me?":
 *   - Bright(c) turns a scene color into a 0-1 "how far over the threshold" value (soft knee, no hard cutoff).
 *   - Bright() is averaged over a disk of taps laid out on a golden-angle spiral (even coverage for any tap
 *     count), with nearer taps weighing more.
 *   - Rotating the spiral per pixel (jitter) turns the banding of a few large taps into fine noise.
 *   - With Keep Highlights Clean the halo fades out on the bright pixels themselves, so the light keeps its own
 *     color and only its surroundings glow.
 *  The tap layout is computed on the CPU (constants), so the shader only does the reads and adds.
 *
 * Output
 *  0 Glow  Tint * Intensity * halo. Add it to the scene color (e.g. Vrr Film Lens + Glow).
 */

#include "MF_Halation.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

namespace VrrHalation
{
	/** 137.5 degrees in radians: consecutive spiral taps never line up, so the disk is covered evenly. */
	constexpr float GoldenAngle = 2.39996323f;
}

UMaterialExpressionVrrHalation::UMaterialExpressionVrrHalation(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Halation"), { { TEXT("Glow"), MCT_Float3 } });
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrHalation::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	const int32 One = Compiler->Constant(1.0f);
	const int32 BaseUV = ViewportUV(Compiler, UV);

	// 1. Bright(c) = saturate((Luminance(c) - Threshold) / Knee): 0 below the threshold, 1 once well above it
	const int32 ThresholdArg = InputOr(Compiler, ThresholdIn, Threshold);
	const int32 KneeArg = Compiler->Constant(FMath::Max(Knee, 0.001f));
	auto Bright = [&](int32 Color)
	{
		return Compiler->Saturate(Compiler->Div(Compiler->Sub(Luminance(Compiler, Color), ThresholdArg), KneeArg));
	};

	// 2. Disk radius in UV (Radius is in 1080p pixels)
	const int32 RadiusUV = Compiler->Mul(PixelAt1080(Compiler), InputOr(Compiler, RadiusIn, Radius));

	// 3. Per-pixel rotation of the tap pattern (cos, sin of a random angle per screen pixel)
	int32 Cos = INDEX_NONE;
	int32 Sin = INDEX_NONE;
	if (bJitter)
	{
		const int32 Pixel = Compiler->Floor(Compiler->Mul(BaseUV, ViewSize(Compiler)));
		const int32 Angle = Compiler->Mul(Hash12(Compiler, Pixel), Compiler->Constant(UE_TWO_PI));
		Cos = Compiler->Cosine(Angle);
		Sin = Compiler->Sine(Angle);
	}

	// 4. Weighted average of Bright() over the disk
	const int32 NumTaps = FMath::Clamp(Samples, 4, 64);
	int32 Sum = Compiler->Constant(0.0f);
	float TotalWeight = 0.0f;
	for (int32 Index = 0; Index < NumTaps; ++Index)
	{
		// 4a. Tap position on the unit disk: sqrt keeps equal area per tap, the golden angle spreads them around
		const float TapRadius = FMath::Sqrt((Index + 0.5f) / NumTaps);
		const float TapAngle = Index * VrrHalation::GoldenAngle;
		const float TapX = TapRadius * FMath::Cos(TapAngle);
		const float TapY = TapRadius * FMath::Sin(TapAngle);
		const float Weight = 1.0f - 0.75f * TapRadius;	// nearer taps weigh more (1 at the center, 0.25 at the rim)

		// 4b. Rotate by the per-pixel angle: (x cos - y sin, x sin + y cos)
		const int32 Offset = bJitter
			? Rotate2D(Compiler, Compiler->Constant(TapX), Compiler->Constant(TapY), Cos, Sin)
			: Compiler->Constant2(TapX, TapY);

		// 4c. Read the scene there and accumulate its brightness
		const int32 Tap = SampleScene(Compiler, Compiler->Add(BaseUV, Compiler->Mul(Offset, RadiusUV)));
		Sum = Compiler->Add(Sum, Compiler->Mul(Bright(Tap), Compiler->Constant(Weight)));
		TotalWeight += Weight;
	}
	int32 Halo = Compiler->Mul(Sum, Compiler->Constant(1.0f / TotalWeight));

	// 5. Keep the highlights themselves clean: fade the halo where this pixel is bright
	if (bKeepHighlightsClean)
	{
		Halo = Compiler->Mul(Halo, Compiler->Sub(One, Bright(SampleScene(Compiler, BaseUV))));
	}

	// 6. Color it
	const int32 TintArg = ColorOr(Compiler, TintIn, Tint);
	return Compiler->Mul(Compiler->Mul(TintArg, InputOr(Compiler, IntensityIn, Intensity)), Halo);
}
#endif
