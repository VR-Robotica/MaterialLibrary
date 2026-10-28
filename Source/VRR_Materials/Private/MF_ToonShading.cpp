/*
 * Vrr Toon Shading: cel shading from one art-directed fake light. Scene lights are ignored.
 *
 * Strategy
 *  The material is Unlit and this node computes the whole look as Emissive, so the art direction (light direction,
 *  colors, band count) is fully controllable and identical on every platform. Three masks are built from the usual
 *  lighting vectors, then mixed into the final color:
 *   - Light mask: how lit the surface is (NdotL), cut into flat bands with a thin soft edge between them.
 *   - Specular:   a hard-edged highlight where the half vector lines up with the normal (NdotH near 1).
 *   - Rim:        a band along the silhouette (normal facing away from the camera), optionally only on the lit side.
 *   - Bounce:     an optional second, weaker fake light (default: from below, like light off the ground) that
 *                 adds a flat tinted band to the shadow side, so shadows don't read as one dead flat color.
 *                 It's banded with the same Softness as the key light so it stays cel-shaded, and it's compiled
 *                 out entirely while Bounce Intensity is 0 and its pin isn't wired.
 *   - Edge line:  an optional thin light line on the shadow side of every band edge, the slightly lighter rim a
 *                 painted cel shadow often has. Its width is measured in screen pixels (from the band coordinate's
 *                 screen derivatives), so it reads like a drawn line however curved the surface is. It lifts the
 *                 diffuse tone toward the next band (Light Mask output stays clean for specular, rim and bounce),
 *                 skips cast shadows, and Edge Line Mask can break it up (e.g. noise). Edge Line Color Blend
 *                 instead paints the line in Edge Line Color (white by default), on top of everything, so it is
 *                 bright enough for post-process halation / bloom to glow around it.
 *  Band Offset shifts NdotL (and the bounce dot) before banding. Inside a flat band that changes nothing, so it only
 *  moves the edges: feed it small animated noise and the terminator wobbles like hand-drawn shading.
 *  Each mask is also an output, so materials can mix them their own way. Cast shadows come in through Shadow
 *  (Vrr Toon Shadow) and darken the light mask, so specular and rim respect them too. Their color either follows
 *  the shadow side (Shadow Color) or, with Cast Shadow Color Blend, multiplies the lit color by Cast Shadow Color,
 *  like Vrr Blob Shadow does on the floor, so cast and blob shadows match.
 *  MF_ToonLit feeds this node from MPC_ToonLighting so every toon material shares one light.
 *
 * Steps
 *  1. Vectors: N (normal, from a normal map if connected), L (to the light), V (to the camera).
 *  2. Light mask: NdotL (+ Band Offset) above Threshold, remapped to 0-1, split into Bands, times Shadow.
 *  3. Specular mask: smoothstep around NdotH = 1 - SpecularSize, lit side only.
 *  4. Rim mask: smoothstep around (1 - NdotV) = 1 - RimWidth, faded toward the lit side by RimLightAlign.
 *  5. Bounce mask: smoothstep around dot(N, BounceDirection) (+ Band Offset) = BounceThreshold, times (1 - Light Mask).
 *  6. Edge line: distance below the next band edge in screen pixels = (ceil(b) - b) / fwidth(b), b = unclamped band
 *     coordinate; line = 1 - distance / width, only for real edges, outside cast shadows, times mask and intensity.
 *  7. Final color: BaseColor * (lerp(Shadow, Light, Light Mask + line * (1 - color blend) / Bands) * cast tint
 *     + bounce) + specular + rim, then lerp toward Edge Line Color by line * color blend. The cast tint multiplies
 *     the lit side by Cast Shadow Color inside cast shadows (by Cast Shadow Color Blend), like the blob shadow.
 *
 * Outputs
 *  0 Emissive       final color, for an Unlit material's Emissive Color
 *  1 Light Mask     0-1
 *  2 Specular Mask  0-1
 *  3 Rim Mask       0-1
 *  4 Bounce Mask    0-1 (0 everywhere while the bounce is off)
 *  5 Edge Line Mask 0-1 times intensity (0 everywhere while the line is off)
 */

#include "MF_ToonShading.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

namespace VrrToonShading
{
	enum EOutput : int32 { Emissive = 0, LightMask = 1, SpecularMask = 2, RimMask = 3, BounceMask = 4, EdgeLineMask = 5 };

	FVector3f ToVector(const FLinearColor& Color) { return FVector3f(Color.R, Color.G, Color.B); }
}

UMaterialExpressionVrrToonShading::UMaterialExpressionVrrToonShading(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Toon Shading"), { TEXT("Emissive"), TEXT("Light Mask"), TEXT("Specular Mask"), TEXT("Rim Mask"), TEXT("Bounce Mask"), TEXT("Edge Line Mask") });
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrToonShading::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;
	using VrrToonShading::ToVector;

	const int32 One = Compiler->Constant(1.0f);
	const int32 MinSoftness = Compiler->Constant(0.0005f);	// keeps the smoothstep edges from dividing by zero

	// 1. Vectors: surface normal, direction to the fake light, direction to the camera
	int32 N;
	if (IsConnected(Normal))
	{
		N = Float3Or(Compiler, Normal, FVector3f::UnitZ());
		if (bNormalIsTangentSpace)
		{
			N = Compiler->TransformVector(MCB_Tangent, MCB_World, N);
		}
		N = Compiler->Normalize(N);
	}
	else
	{
		N = Compiler->PixelNormalWS();
	}
	const int32 L = Compiler->Normalize(Float3Or(Compiler, LightDirectionIn, FVector3f(LightDirection)));
	const int32 V = Compiler->CameraVector();
	const int32 NdotL = Compiler->Dot(N, L);

	// 1b. Band Offset moves the band edges only (specular and rim keep the true NdotL)
	const bool bHasBandOffset = IsConnected(BandOffset);
	const int32 BandOffsetArg = bHasBandOffset ? Compiler->ValidCast(BandOffset.Compile(Compiler), MCT_Float1) : INDEX_NONE;
	const int32 BandNdotL = bHasBandOffset ? Compiler->Add(NdotL, BandOffsetArg) : NdotL;

	// 2. Light mask
	const int32 ThresholdArg = InputOr(Compiler, ThresholdIn, Threshold);
	const int32 SoftnessArg = Compiler->Max(InputOr(Compiler, SoftnessIn, Softness), MinSoftness);
	const int32 BandsArg = Compiler->Max(Compiler->Floor(InputOr(Compiler, BandsIn, Bands)), One);

	// 2a. Lit part (NdotL from Threshold to 1) remapped to 0-1, then scaled so each band is one unit wide
	const int32 LitRange = Compiler->Max(Compiler->Sub(One, ThresholdArg), Compiler->Constant(0.001f));
	const int32 BandPos = Compiler->Mul(Compiler->Saturate(Compiler->Div(Compiler->Sub(BandNdotL, ThresholdArg), LitRange)), BandsArg);

	// 2b. Inside each band, ramp up over the first EdgeWidth (Softness in NdotL units, converted to band units)
	const int32 EdgeWidth = Compiler->Div(Compiler->Mul(SoftnessArg, BandsArg), LitRange);
	const int32 BandStep = Compiler->Saturate(Compiler->Div(Compiler->Frac(BandPos), EdgeWidth));

	// 2c. Whole bands passed + the ramp of the current one, back to 0-1
	int32 LightMask = Compiler->Div(Compiler->Add(Compiler->Floor(BandPos), BandStep), BandsArg);

	// 2d. Cast shadows darken it (the mask without them is kept for blob-style cast shadow color, 7a)
	const bool bHasShadow = IsConnected(Shadow);
	const int32 FormMask = LightMask;
	const int32 CastLit = bHasShadow ? Compiler->Saturate(Shadow.Compile(Compiler)) : INDEX_NONE;	// 1 = outside cast shadows
	if (bHasShadow)
	{
		LightMask = Compiler->Mul(LightMask, CastLit);
	}
	if (OutputIndex == VrrToonShading::LightMask)
	{
		return LightMask;
	}

	// 3. Specular: hard-edged highlight where the half vector lines up with the normal (half the band softness)
	const int32 H = Compiler->Normalize(Compiler->Add(L, V));
	const int32 NdotH = Compiler->Dot(N, H);
	const int32 SpecEdge = Compiler->Sub(One, InputOr(Compiler, SpecularSizeIn, SpecularSize));
	const int32 SpecSoftness = Compiler->Max(Compiler->Mul(SoftnessArg, Compiler->Constant(0.5f)), MinSoftness);
	const int32 SpecularMask = Compiler->Mul(
		Compiler->SmoothStep(Compiler->Sub(SpecEdge, SpecSoftness), Compiler->Add(SpecEdge, SpecSoftness), NdotH),
		LightMask);
	if (OutputIndex == VrrToonShading::SpecularMask)
	{
		return SpecularMask;
	}

	// 4. Rim: band along the silhouette, optionally faded out on the shadow side
	const int32 Fresnel = Compiler->Sub(One, Compiler->Saturate(Compiler->Dot(N, V)));
	const int32 RimEdge = Compiler->Sub(One, InputOr(Compiler, RimWidthIn, RimWidth));
	const int32 RimShape = Compiler->SmoothStep(Compiler->Sub(RimEdge, SoftnessArg), Compiler->Add(RimEdge, SoftnessArg), Fresnel);
	const int32 RimMask = Compiler->Mul(RimShape, Compiler->Lerp(One, LightMask, Compiler->Constant(RimLightAlign)));
	if (OutputIndex == VrrToonShading::RimMask)
	{
		return RimMask;
	}

	// 5. Bounce: skipped entirely (mask 0) while it's off, so materials that don't use it pay nothing
	const bool bBounceOn = BounceIntensity > 0.0f || IsConnected(BounceIntensityIn);
	int32 BounceMask = Compiler->Constant(0.0f);
	if (bBounceOn)
	{
		// 5a. Flat band facing the bounce source, with the same soft edge (and edge wobble) as the light bands
		const int32 B = Compiler->Normalize(Float3Or(Compiler, BounceDirectionIn, FVector3f(BounceDirection)));
		const int32 Edge = Compiler->Constant(BounceThreshold);
		int32 NdotB = Compiler->Dot(N, B);
		if (bHasBandOffset)
		{
			NdotB = Compiler->Add(NdotB, BandOffsetArg);
		}
		BounceMask = Compiler->SmoothStep(Compiler->Sub(Edge, SoftnessArg), Compiler->Add(Edge, SoftnessArg), NdotB);

		// 5b. Classic bounce only fills the shadow side (including cast shadows, since Light Mask already has them)
		if (bBounceInShadowOnly)
		{
			BounceMask = Compiler->Mul(BounceMask, Compiler->Sub(One, LightMask));
		}
	}
	if (OutputIndex == VrrToonShading::BounceMask)
	{
		return BounceMask;
	}

	// 6. Edge line: skipped entirely (mask 0) while it's off
	const bool bEdgeLineOn = EdgeLineIntensity > 0.0f || IsConnected(EdgeLineIntensityIn);
	int32 EdgeLine = Compiler->Constant(0.0f);
	if (bEdgeLineOn)
	{
		// 6a. Unclamped band coordinate: band edges sit at whole numbers 0 .. Bands-1, shadow side just below each
		const int32 BandCoord = Compiler->Mul(Compiler->Div(Compiler->Sub(BandNdotL, ThresholdArg), LitRange), BandsArg);
		const int32 NextEdge = Compiler->Ceil(BandCoord);

		// 6b. Distance below the next edge in screen pixels: band units / band units per pixel (fwidth)
		const int32 PerPixel = Compiler->Max(
			Compiler->Add(Compiler->Abs(Compiler->DDX(BandCoord)), Compiler->Abs(Compiler->DDY(BandCoord))),
			Compiler->Constant(1e-5f));
		const int32 DistancePx = Compiler->Div(Compiler->Sub(NextEdge, BandCoord), PerPixel);

		// 6c. Width given in 1080p pixels, scaled to this view's height
		const int32 ViewHeight = Y(Compiler, Compiler->ViewProperty(MEVP_ViewSize));
		const int32 WidthPx = Compiler->Max(
			Compiler->Mul(InputOr(Compiler, EdgeLineWidthIn, EdgeLineWidth), Compiler->Div(ViewHeight, Compiler->Constant(1080.0f))),
			Compiler->Constant(0.5f));
		EdgeLine = Compiler->Saturate(Compiler->Sub(One, Compiler->Div(DistancePx, WidthPx)));

		// 6d. Only real edges: NextEdge in 0 .. Bands-1 (whole numbers, so saturate works as a step)
		EdgeLine = Compiler->Mul(EdgeLine, Compiler->Mul(
			Compiler->Saturate(Compiler->Add(NextEdge, One)),
			Compiler->Saturate(Compiler->Sub(BandsArg, NextEdge))));

		// 6e. No line inside cast shadows (the terminator isn't visible there), then breakup and intensity
		if (bHasShadow)
		{
			EdgeLine = Compiler->Mul(EdgeLine, CastLit);
		}
		if (IsConnected(EdgeLineMask))
		{
			EdgeLine = Compiler->Mul(EdgeLine, Compiler->Saturate(Compiler->ValidCast(EdgeLineMask.Compile(Compiler), MCT_Float1)));
		}
		EdgeLine = Compiler->Mul(EdgeLine, InputOr(Compiler, EdgeLineIntensityIn, EdgeLineIntensity));
	}
	if (OutputIndex == VrrToonShading::EdgeLineMask)
	{
		return EdgeLine;
	}

	// 7. Final color
	const int32 BaseColorArg = Float3Or(Compiler, BaseColorIn, ToVector(BaseColor));
	const int32 LightColorArg = Float3Or(Compiler, LightColorIn, ToVector(LightColor));
	const int32 ShadowColorArg = Float3Or(Compiler, ShadowColorIn, ToVector(ShadowColor));
	const int32 RimColorArg = Float3Or(Compiler, RimColorIn, ToVector(RimColor));

	// 7a. Diffuse: base color lit by the light color, or tinted by the shadow color (the ambient), plus the bounce
	//     light added to that lighting before it multiplies the base color (so it tints like real light does).
	//     The edge line lifts the shadow side toward the next band's tone (one band = 1 / Bands of the mask).
	//     With a color blend, that share of the line is painted in Edge Line Color at the end (7c) instead.
	//     Cast Shadow Color Blend b: the mask keeps 1 - b of the cast shadow, and the other b becomes a multiply by
	//     Cast Shadow Color on the lit side only (the shadow side is already shadow toned), the way the blob shadow's
	//     Modulate box multiplies the floor.
	const bool bEdgeColorOn = bEdgeLineOn && (EdgeLineColorBlend > 0.0f || IsConnected(EdgeLineColorBlendIn));
	const int32 ColorBlendArg = bEdgeColorOn ? Compiler->Saturate(InputOr(Compiler, EdgeLineColorBlendIn, EdgeLineColorBlend)) : INDEX_NONE;
	const bool bCastColorOn = bHasShadow && (CastShadowColorBlend > 0.0f || IsConnected(CastShadowColorBlendIn));
	const int32 CastBlendArg = bCastColorOn ? Compiler->Saturate(InputOr(Compiler, CastShadowColorBlendIn, CastShadowColorBlend)) : INDEX_NONE;
	int32 DiffuseMask = bCastColorOn ? Compiler->Lerp(LightMask, FormMask, CastBlendArg) : LightMask;
	if (bEdgeLineOn)
	{
		const int32 ToneLine = bEdgeColorOn ? Compiler->Mul(EdgeLine, Compiler->Sub(One, ColorBlendArg)) : EdgeLine;
		DiffuseMask = Compiler->Saturate(Compiler->Add(DiffuseMask, Compiler->Div(ToneLine, BandsArg)));
	}
	int32 Lighting = Compiler->Lerp(ShadowColorArg, LightColorArg, DiffuseMask);
	if (bCastColorOn)
	{
		const int32 CastColorArg = Float3Or(Compiler, CastShadowColorIn, ToVector(CastShadowColor));
		const int32 CastAmount = Compiler->Mul(Compiler->Mul(Compiler->Sub(One, CastLit), CastBlendArg), FormMask);
		Lighting = Compiler->Mul(Lighting, Compiler->Lerp(Compiler->Constant3(1.0f, 1.0f, 1.0f), CastColorArg, CastAmount));
	}
	if (bBounceOn)
	{
		const int32 BounceColorArg = Float3Or(Compiler, BounceColorIn, ToVector(BounceColor));
		const int32 BounceStrength = Compiler->Mul(BounceMask, InputOr(Compiler, BounceIntensityIn, BounceIntensity));
		Lighting = Compiler->Add(Lighting, Compiler->Mul(BounceColorArg, BounceStrength));
	}
	const int32 Diffuse = Compiler->Mul(BaseColorArg, Lighting);
	// 7b. Highlight and rim are added on top
	const int32 Specular = Compiler->Mul(LightColorArg, Compiler->Mul(SpecularMask, InputOr(Compiler, SpecularIntensityIn, SpecularIntensity)));
	const int32 Rim = Compiler->Mul(RimColorArg, Compiler->Mul(RimMask, InputOr(Compiler, RimIntensityIn, RimIntensity)));

	int32 Result = Compiler->Add(Diffuse, Compiler->Add(Specular, Rim));

	// 7c. Colored edge line painted over the result (white by default, so halation / bloom catch it)
	if (bEdgeColorOn)
	{
		const int32 EdgeColorArg = Float3Or(Compiler, EdgeLineColorIn, ToVector(EdgeLineColor));
		Result = Compiler->Lerp(Result, EdgeColorArg, Compiler->Saturate(Compiler->Mul(EdgeLine, ColorBlendArg)));
	}
	return Result;
}
#endif
