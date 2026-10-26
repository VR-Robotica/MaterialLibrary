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
 *  Each mask is also an output, so materials can mix them their own way. Cast shadows come in through Shadow
 *  (Vrr Toon Shadow) and darken the light mask, so specular and rim respect them too.
 *  MF_ToonLit feeds this node from MPC_ToonLighting so every toon material shares one light.
 *
 * Steps
 *  1. Vectors: N (normal, from a normal map if connected), L (to the light), V (to the camera).
 *  2. Light mask: NdotL above Threshold, remapped to 0-1, split into Bands, times Shadow.
 *  3. Specular mask: smoothstep around NdotH = 1 - SpecularSize, lit side only.
 *  4. Rim mask: smoothstep around (1 - NdotV) = 1 - RimWidth, faded toward the lit side by RimLightAlign.
 *  5. Final color: BaseColor * lerp(Shadow, Light) + specular + rim.
 *
 * Outputs
 *  0 Emissive       final color, for an Unlit material's Emissive Color
 *  1 Light Mask     0-1
 *  2 Specular Mask  0-1
 *  3 Rim Mask       0-1
 */

#include "MF_ToonShading.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

namespace VrrToonShading
{
	enum EOutput : int32 { Emissive = 0, LightMask = 1, SpecularMask = 2, RimMask = 3 };

	FVector3f ToVector(const FLinearColor& Color) { return FVector3f(Color.R, Color.G, Color.B); }
}

UMaterialExpressionVrrToonShading::UMaterialExpressionVrrToonShading(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Toon Shading"), { TEXT("Emissive"), TEXT("Light Mask"), TEXT("Specular Mask"), TEXT("Rim Mask") });
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

	// 2. Light mask
	const int32 ThresholdArg = InputOr(Compiler, ThresholdIn, Threshold);
	const int32 SoftnessArg = Compiler->Max(InputOr(Compiler, SoftnessIn, Softness), MinSoftness);
	const int32 BandsArg = Compiler->Max(Compiler->Floor(InputOr(Compiler, BandsIn, Bands)), One);

	// 2a. Lit part (NdotL from Threshold to 1) remapped to 0-1, then scaled so each band is one unit wide
	const int32 LitRange = Compiler->Max(Compiler->Sub(One, ThresholdArg), Compiler->Constant(0.001f));
	const int32 BandPos = Compiler->Mul(Compiler->Saturate(Compiler->Div(Compiler->Sub(NdotL, ThresholdArg), LitRange)), BandsArg);

	// 2b. Inside each band, ramp up over the first EdgeWidth (Softness in NdotL units, converted to band units)
	const int32 EdgeWidth = Compiler->Div(Compiler->Mul(SoftnessArg, BandsArg), LitRange);
	const int32 BandStep = Compiler->Saturate(Compiler->Div(Compiler->Frac(BandPos), EdgeWidth));

	// 2c. Whole bands passed + the ramp of the current one, back to 0-1
	int32 LightMask = Compiler->Div(Compiler->Add(Compiler->Floor(BandPos), BandStep), BandsArg);

	// 2d. Cast shadows darken it
	if (IsConnected(Shadow))
	{
		LightMask = Compiler->Mul(LightMask, Compiler->Saturate(Shadow.Compile(Compiler)));
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

	// 5. Final color
	const int32 BaseColorArg = Float3Or(Compiler, BaseColorIn, ToVector(BaseColor));
	const int32 LightColorArg = Float3Or(Compiler, LightColorIn, ToVector(LightColor));
	const int32 ShadowColorArg = Float3Or(Compiler, ShadowColorIn, ToVector(ShadowColor));
	const int32 RimColorArg = Float3Or(Compiler, RimColorIn, ToVector(RimColor));

	// 5a. Diffuse: base color lit by the light color, or tinted by the shadow color (the ambient)
	const int32 Diffuse = Compiler->Mul(BaseColorArg, Compiler->Lerp(ShadowColorArg, LightColorArg, LightMask));
	// 5b. Highlight and rim are added on top
	const int32 Specular = Compiler->Mul(LightColorArg, Compiler->Mul(SpecularMask, InputOr(Compiler, SpecularIntensityIn, SpecularIntensity)));
	const int32 Rim = Compiler->Mul(RimColorArg, Compiler->Mul(RimMask, InputOr(Compiler, RimIntensityIn, RimIntensity)));

	return Compiler->Add(Diffuse, Compiler->Add(Specular, Rim));
}
#endif
