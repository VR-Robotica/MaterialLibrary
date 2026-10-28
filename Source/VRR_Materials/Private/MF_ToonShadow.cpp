/*
 * Vrr Toon Shadow: cast shadows for toon materials from the fake light (1 = lit, 0 = in shadow).
 *
 * Strategy
 *  A classic shadow map, but driven by our own capture instead of a scene light, since toon materials are Unlit:
 *   - AVrr_ToonShadowCaster renders an orthographic depth map looking along the fake light and writes that view
 *     (origin, axes, width, resolution) into MPC_ToonLighting every frame.
 *   - This node projects the pixel's world position into that view and compares depths: if something in the map
 *     is closer to the light than the pixel, the pixel is in shadow.
 *  Shadow-map artifacts are handled the usual way:
 *   - Acne (lit surfaces shadowing themselves): a depth bias in cm, plus one that scales with texel size, plus a
 *     slope-scaled one that grows as the surface turns away from the light (where acne shows as a diamond pattern
 *     on curved surfaces); the sample point is also pushed off the surface along the normal.
 *   - Hard, pixelated edges: optional 5-tap filtering (center + 4 diagonals at SoftnessTexels).
 *   - A visible edge where the map ends: shadows fade out near its border.
 *  Optional distortion pushes the map lookup around with smooth world-space noise (and time), so the edges ripple
 *  slightly like Vrr Blob Shadow's hand-drawn edge; the slope bias grows by the same reach so it adds no acne.
 *  With no caster in the level the map width is 0, and the node returns fully lit.
 *
 * Steps
 *  1. Read the capture's view from the Shadow* inputs.
 *  2. Pixel position relative to the capture, pushed along the normal.
 *  3. Project into map UV and depth, minus the bias (constant + texel + slope + distortion reach), then distort
 *     the lookup UV.
 *  4. Depth test (1 or 5 taps).
 *  5. Fade near the border and when no caster is active.
 *
 * Output
 *  0 Shadow  0-1, plug into Vrr Toon Shading's Shadow
 */

#include "MF_ToonShadow.h"

#include "Engine/Texture.h"
#include "MaterialCompiler.h"
#include "Materials/MaterialExpressionUtils.h"
#include "VrrMaterialMath.h"

UMaterialExpressionVrrToonShadow::UMaterialExpressionVrrToonShadow(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Toon Shadow"), { TEXT("Shadow") });
}

UObject* UMaterialExpressionVrrToonShadow::GetReferencedTexture() const
{
	return ShadowMap.Get();
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrToonShadow::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	if (!ShadowMap)
	{
		return NodeError(Compiler, TEXT("set a Shadow Map render target"));
	}
	if (!IsConnected(ShadowOrigin) || !IsConnected(ShadowRight) || !IsConnected(ShadowUp) || !IsConnected(ShadowForward))
	{
		return NodeError(Compiler, TEXT("connect ShadowOrigin, ShadowRight, ShadowUp and ShadowForward (MPC_ToonLighting)"));
	}
	const int32 Zero = Compiler->Constant(0.0f);
	const int32 One = Compiler->Constant(1.0f);
	const int32 Half = Compiler->Constant(0.5f);

	// 1. Depth map view, written to the collection by AVrr_ToonShadowCaster
	const int32 OriginArg = ShadowOrigin.Compile(Compiler);
	const int32 RightArg = ShadowRight.Compile(Compiler);
	const int32 RawWidth = W(Compiler, OriginArg);						// map width in cm, 0 = no caster
	const int32 Width = Compiler->Max(RawWidth, One);
	const int32 Resolution = Compiler->Max(W(Compiler, RightArg), One);
	const int32 TexelSize = Compiler->Div(Width, Resolution);			// cm per texel
	const int32 Right = XYZ(Compiler, RightArg);
	const int32 Up = XYZ(Compiler, ShadowUp.Compile(Compiler));
	const int32 Forward = XYZ(Compiler, ShadowForward.Compile(Compiler));

	// 2. Pixel position relative to the capture (float3 is precise enough this close), pushed off the surface
	const int32 N = IsConnected(Normal) ? Compiler->Normalize(Normal.Compile(Compiler)) : Compiler->VertexNormal();
	int32 Relative = Compiler->ValidCast(Compiler->Sub(Compiler->WorldPosition(WPT_Default), XYZ(Compiler, OriginArg)), MCT_Float3);
	Relative = Compiler->Add(Relative, Compiler->Mul(N, Compiler->Mul(TexelSize, Compiler->Constant(NormalOffset))));

	// 3. Project into the map: U along Right, V down along -Up (both -0.5..0.5 across the map), depth along Forward
	const int32 MapX = Compiler->Div(Compiler->Dot(Relative, Right), Width);
	const int32 MapY = Compiler->Div(Compiler->Dot(Relative, Up), Width);
	const int32 UV = Compiler->AppendVector(Compiler->Add(MapX, Half), Compiler->Sub(Half, MapY));

	// 3a. Depth bias in cm: constant + texel-sized + slope-scaled. On a surface tilted from the light by angle theta,
	//     depth changes by tan(theta) cm per cm across the map, so one texel (and each Softness tap, up to
	//     Softness * sqrt(2) texels away diagonally) can read a depth that much closer than the pixel's own surface.
	//     cos(theta) is clamped to 0.2 (tan about 5) so the bias stays bounded at the terminator and on the back side,
	//     which the light mask already shades. The distortion (3b) reads the map up to Distortion cm away, so it adds
	//     tan(theta) * Distortion cm the same way.
	const bool bDistort = Distortion > 0.0f || IsConnected(DistortionIn);
	const int32 DistortionArg = bDistort ? Compiler->Max(InputOr(Compiler, DistortionIn, Distortion), Zero) : INDEX_NONE;
	int32 BiasTexels = Compiler->Constant(TexelBias);
	int32 TanTheta = INDEX_NONE;
	if (SlopeBias > 0.0f || bDistort)
	{
		const int32 CosTheta = Compiler->Max(Compiler->Dot(N, Compiler->Sub(Zero, Forward)), Compiler->Constant(0.2f));
		TanTheta = Compiler->Div(Compiler->SquareRoot(Compiler->Max(Compiler->Sub(One, Compiler->Mul(CosTheta, CosTheta)), Zero)), CosTheta);
	}
	if (SlopeBias > 0.0f)
	{
		const float TapReach = 1.0f + FMath::Max(SoftnessTexels, 0.0f) * UE_SQRT_2;	// texels from the pixel to its farthest tap
		BiasTexels = Compiler->Add(BiasTexels, Compiler->Mul(TanTheta, Compiler->Constant(SlopeBias * TapReach)));
	}
	int32 Bias = Compiler->Add(Compiler->Constant(DepthBias), Compiler->Mul(TexelSize, BiasTexels));
	if (bDistort)
	{
		Bias = Compiler->Add(Bias, Compiler->Mul(TanTheta, DistortionArg));
	}
	const int32 PixelDepth = Compiler->Sub(Compiler->Dot(Relative, Forward), Bias);

	// 3b. Distortion: push the map lookup around by two smooth -1..1 noises (one per axis), sampled on the pixel's
	//     position across the map (in meters, so the pattern sticks to the world, not the screen) and on Time.
	//     Only the lookup moves, never the pixel's own depth, so the shadow's edges ripple while its inside stays solid.
	int32 LookupUV = UV;
	if (bDistort)
	{
		const int32 Time = Compiler->ValidCast(IsConnected(TimeIn) ? TimeIn.Compile(Compiler) : Seconds(Compiler, false), MCT_Float1);
		const int32 PlaneMeters = Compiler->Mul(Compiler->AppendVector(Compiler->Dot(Relative, Right), Compiler->Dot(Relative, Up)), Compiler->Constant(DistortionScale / 100.0f));
		const int32 TimePos = Compiler->Mul(Time, Compiler->Constant(DistortionSpeed));
		// Two unrelated noises: the same pattern shifted far apart in the plane
		auto Wobble = [&](float Salt)
		{
			return GradientNoise(Compiler, Compiler->AppendVector(Compiler->Add(PlaneMeters, Compiler->Constant2(Salt, -Salt)), TimePos));
		};
		const int32 OffsetCm = Compiler->Mul(Compiler->AppendVector(Wobble(0.0f), Wobble(37.0f)), DistortionArg);
		LookupUV = Compiler->Add(UV, Compiler->Div(OffsetCm, Width));
	}

	// 4. Depth test: lit (1) when nothing in the map is closer to the light than this pixel
	int32 TextureReferenceIndex = INDEX_NONE;
	const EMaterialSamplerType SamplerType = MaterialExpressionUtils::GetSamplerTypeForTexture(ShadowMap);
	const int32 TextureCode = Compiler->Texture(ShadowMap, TextureReferenceIndex, SamplerType, SSM_Clamp_WorldGroupSettings);
	auto LitAt = [&](int32 SampleUV)
	{
		const int32 Sample = Compiler->TextureSample(TextureCode, SampleUV, SamplerType, INDEX_NONE, INDEX_NONE, TMVM_None,
			SSM_Clamp_WorldGroupSettings, TGM_None, TextureReferenceIndex);
		return Compiler->Step(PixelDepth, X(Compiler, Sample));
	};

	int32 Lit;
	if (SoftnessTexels <= 0.0f)
	{
		// 4a. One hard sample
		Lit = LitAt(LookupUV);
	}
	else
	{
		// 4b. Center plus four diagonal taps, averaged for a soft edge
		const int32 Radius = Compiler->Div(Compiler->Constant(SoftnessTexels), Resolution);
		const int32 NegRadius = Compiler->Sub(Zero, Radius);
		auto Offset = [&](int32 DU, int32 DV) { return Compiler->Add(LookupUV, Compiler->AppendVector(DU, DV)); };

		Lit = LitAt(LookupUV);
		Lit = Compiler->Add(Lit, LitAt(Offset(Radius, Radius)));
		Lit = Compiler->Add(Lit, LitAt(Offset(NegRadius, Radius)));
		Lit = Compiler->Add(Lit, LitAt(Offset(Radius, NegRadius)));
		Lit = Compiler->Add(Lit, LitAt(Offset(NegRadius, NegRadius)));
		Lit = Compiler->Mul(Lit, Compiler->Constant(0.2f));
	}

	// 5. Fade out near the border of the covered area, and turn off entirely when no caster is active (width 0)
	const int32 BorderDistance = Compiler->Sub(Half, Compiler->Max(Compiler->Abs(MapX), Compiler->Abs(MapY)));
	const int32 Fade = Compiler->Saturate(Compiler->Div(BorderDistance, Compiler->Constant(EdgeFade)));
	const int32 Enabled = Compiler->Step(One, RawWidth);

	return Compiler->Lerp(One, Lit, Compiler->Mul(Fade, Enabled));
}
#endif
