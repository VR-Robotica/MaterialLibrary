// Copyright VR Robotica. All Rights Reserved.

/*
 * Vrr Film Lens: lens imperfections for a Post Process material (chromatic aberration, channel shift, softness).
 *
 * Strategy
 *  Everything is done by where the scene is sampled, never by changing colors afterwards:
 *   - Chromatic aberration: a real lens bends red and blue by slightly different amounts, more toward the edges.
 *     Red is read pushed outward from the screen center and blue pulled inward, green stays in place.
 *   - Channel shift: an extra sideways split (red right, blue left), e.g. Vrr Tape Warp's Color Split.
 *   - Softness: a small 5-tap blur (center + 4 diagonals), like a slightly soft film lens.
 *  Each blur tap does the chromatic split itself, so both effects combine correctly.
 *  Effects that are off (zero and unconnected) are skipped entirely, so they cost no extra samples.
 *
 * Steps
 *  1. Decide which effects are on.
 *  2. Chromatic offset: radial part + channel shift part, in UV.
 *  3. Tap(): one scene read, split into R / G / B reads when chromatic is on.
 *  4. Softness: weighted sum of 5 taps, or a single tap when sharp.
 *
 * Output
 *  0 Color  the filtered scene color
 */

#include "MF_FilmLens.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

UMaterialExpressionVrrFilmLens::UMaterialExpressionVrrFilmLens(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Film Lens"), { { TEXT("Color"), MCT_Float3 } });
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrFilmLens::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	const int32 BaseUV = ViewportUV(Compiler, UV);
	const int32 Size = ViewSize(Compiler);

	// 1. Which effects are on (unconnected and zero = skipped)
	const bool bRadial = IsConnected(ChromaticAberrationIn) || ChromaticAberration > 0.0f;
	const bool bShift = IsConnected(ChannelShift);
	const bool bChromatic = bRadial || bShift;
	const bool bSoft = IsConnected(SoftnessIn) || Softness > 0.0f;

	// 2. Chromatic offset in UV (red gets +offset, blue -offset)
	int32 ChromaticOffset = Compiler->Constant2(0.0f, 0.0f);
	if (bRadial)
	{
		// 2a. Radial: along the direction from the center. r = 0 at the center, 1 in the corners (aspect-corrected),
		//     offset = (UV - 0.5) * 2 * Amount * r^Falloff
		const int32 FromCenter = Compiler->Sub(BaseUV, Compiler->Constant2(0.5f, 0.5f));
		const int32 Aspect = Compiler->AppendVector(Compiler->Div(X(Compiler, Size), Y(Compiler, Size)), Compiler->Constant(1.0f));
		const int32 CornerDistance = Compiler->Mul(Compiler->Length(Aspect), Compiler->Constant(0.5f));
		const int32 R = Compiler->Div(Compiler->Length(Compiler->Mul(FromCenter, Aspect)), CornerDistance);
		const int32 Amount = InputOr(Compiler, ChromaticAberrationIn, ChromaticAberration);
		const int32 Strength = Compiler->Power(Compiler->Max(R, Compiler->Constant(0.0001f)), Compiler->Constant(ChromaticFalloff));
		ChromaticOffset = Compiler->Mul(Compiler->Mul(FromCenter, Compiler->Constant(2.0f)), Compiler->Mul(Amount, Strength));
	}
	if (bShift)
	{
		// 2b. Sideways: Channel Shift is in 1080p pixels, converted to UV
		const int32 ShiftPx = Compiler->ValidCast(ChannelShift.Compile(Compiler), MCT_Float1);
		const int32 ShiftUV = Compiler->Mul(ShiftPx, X(Compiler, PixelAt1080(Compiler)));
		ChromaticOffset = Compiler->Add(ChromaticOffset, Compiler->AppendVector(ShiftUV, Compiler->Constant(0.0f)));
	}

	// 3. One tap: RGB at UV, or R pushed out, G in place, B pulled in
	auto Tap = [&](int32 TapUV)
	{
		if (!bChromatic)
		{
			return SampleScene(Compiler, TapUV);
		}
		const int32 Red = X(Compiler, SampleScene(Compiler, Compiler->Add(TapUV, ChromaticOffset)));
		const int32 Green = Y(Compiler, SampleScene(Compiler, TapUV));
		const int32 Blue = Z(Compiler, SampleScene(Compiler, Compiler->Sub(TapUV, ChromaticOffset)));
		return Compiler->AppendVector(Compiler->AppendVector(Red, Green), Blue);
	};

	if (!bSoft)
	{
		return Tap(BaseUV);
	}

	// 4. Softness: center 40%, four diagonal taps 15% each (weights sum to 1)
	const int32 Radius = Compiler->Mul(PixelAt1080(Compiler), InputOr(Compiler, SoftnessIn, Softness));
	int32 Result = Compiler->Mul(Tap(BaseUV), Compiler->Constant(0.4f));
	for (const FVector2f Diagonal : { FVector2f(1, 1), FVector2f(-1, 1), FVector2f(1, -1), FVector2f(-1, -1) })
	{
		const int32 TapUV = Compiler->Add(BaseUV, Compiler->Mul(Radius, Compiler->Constant2(Diagonal.X, Diagonal.Y)));
		Result = Compiler->Add(Result, Compiler->Mul(Tap(TapUV), Compiler->Constant(0.15f)));
	}
	return Result;
}
#endif
