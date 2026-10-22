#include "MF_ToonShadow.h"

#include "Engine/Texture.h"
#include "MaterialCompiler.h"
#include "Materials/MaterialExpressionTextureBase.h"

#define LOCTEXT_NAMESPACE "VrrMaterialExpressions"

UMaterialExpressionVrrToonShadow::UMaterialExpressionVrrToonShadow(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(LOCTEXT("VRR", "VRR"));

	bShowOutputNameOnPin = true;
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Shadow")));
#endif
}

UObject* UMaterialExpressionVrrToonShadow::GetReferencedTexture() const
{
	return ShadowMap.Get();
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrToonShadow::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!ShadowMap)
	{
		return Compiler->Errorf(TEXT("Vrr Toon Shadow: set a Shadow Map render target"));
	}
	if (!ShadowOrigin.GetTracedInput().Expression || !ShadowRight.GetTracedInput().Expression
		|| !ShadowUp.GetTracedInput().Expression || !ShadowForward.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Vrr Toon Shadow: connect ShadowOrigin, ShadowRight, ShadowUp and ShadowForward (MPC_ToonLighting)"));
	}

	auto XYZ = [Compiler](int32 Arg) { return Compiler->ComponentMask(Arg, true, true, true, false); };
	auto W = [Compiler](int32 Arg) { return Compiler->ComponentMask(Arg, false, false, false, true); };

	const int32 One = Compiler->Constant(1.0f);
	const int32 Half = Compiler->Constant(0.5f);

	// Depth map view, written to the collection by AVrr_ToonShadowCaster
	const int32 OriginArg = ShadowOrigin.Compile(Compiler);
	const int32 RightArg = ShadowRight.Compile(Compiler);
	const int32 RawWidth = W(OriginArg);
	const int32 Width = Compiler->Max(RawWidth, One);
	const int32 Resolution = Compiler->Max(W(RightArg), One);
	const int32 TexelSize = Compiler->Div(Width, Resolution);	// cm per texel
	const int32 Right = XYZ(RightArg);
	const int32 Up = XYZ(ShadowUp.Compile(Compiler));
	const int32 Forward = XYZ(ShadowForward.Compile(Compiler));

	// Pixel position relative to the capture, pushed off the surface along the normal
	const int32 N = Normal.GetTracedInput().Expression
		? Compiler->Normalize(Normal.Compile(Compiler))
		: Compiler->VertexNormal();
	int32 Relative = Compiler->ValidCast(
		Compiler->Sub(Compiler->WorldPosition(WPT_Default), XYZ(OriginArg)), MCT_Float3);
	Relative = Compiler->Add(Relative, Compiler->Mul(N, Compiler->Mul(TexelSize, Compiler->Constant(NormalOffset))));

	// Project into the map: U along Right, V down along -Up, depth along Forward
	const int32 X = Compiler->Div(Compiler->Dot(Relative, Right), Width);
	const int32 Y = Compiler->Div(Compiler->Dot(Relative, Up), Width);
	const int32 U = Compiler->Add(X, Half);
	const int32 V = Compiler->Sub(Half, Y);
	const int32 UV = Compiler->AppendVector(U, V);

	const int32 Bias = Compiler->Add(Compiler->Constant(DepthBias), Compiler->Mul(TexelSize, Compiler->Constant(TexelBias)));
	const int32 PixelDepth = Compiler->Sub(Compiler->Dot(Relative, Forward), Bias);

	// Depth test: lit when nothing in the map is closer to the light than this pixel
	int32 TextureReferenceIndex = INDEX_NONE;
	const EMaterialSamplerType SamplerType = UMaterialExpressionTextureBase::GetSamplerTypeForTexture(ShadowMap);
	const int32 TextureCode = Compiler->Texture(ShadowMap, TextureReferenceIndex, SamplerType, SSM_Clamp_WorldGroupSettings);
	auto LitAt = [&](int32 SampleUV)
	{
		const int32 Sample = Compiler->TextureSample(TextureCode, SampleUV, SamplerType, INDEX_NONE, INDEX_NONE, TMVM_None,
			SSM_Clamp_WorldGroupSettings, TGM_None, TextureReferenceIndex);
		const int32 MapDepth = Compiler->ComponentMask(Sample, true, false, false, false);
		return Compiler->Step(PixelDepth, MapDepth);
	};

	int32 Lit;
	if (SoftnessTexels <= 0.0f)
	{
		Lit = LitAt(UV);
	}
	else
	{
		// Center plus four diagonal taps, averaged for a soft edge
		const int32 Radius = Compiler->Div(Compiler->Constant(SoftnessTexels), Resolution);
		const int32 NegRadius = Compiler->Sub(Compiler->Constant(0.0f), Radius);
		auto Offset = [&](int32 DU, int32 DV) { return Compiler->Add(UV, Compiler->AppendVector(DU, DV)); };

		Lit = LitAt(UV);
		Lit = Compiler->Add(Lit, LitAt(Offset(Radius, Radius)));
		Lit = Compiler->Add(Lit, LitAt(Offset(NegRadius, Radius)));
		Lit = Compiler->Add(Lit, LitAt(Offset(Radius, NegRadius)));
		Lit = Compiler->Add(Lit, LitAt(Offset(NegRadius, NegRadius)));
		Lit = Compiler->Mul(Lit, Compiler->Constant(0.2f));
	}

	// Fade out near the border of the covered area, and turn off entirely when no caster is active (width 0)
	const int32 BorderDistance = Compiler->Sub(Half, Compiler->Max(Compiler->Abs(X), Compiler->Abs(Y)));
	const int32 Fade = Compiler->Saturate(Compiler->Div(BorderDistance, Compiler->Constant(EdgeFade)));
	const int32 Enabled = Compiler->Step(One, RawWidth);

	return Compiler->Lerp(One, Lit, Compiler->Mul(Fade, Enabled));
}

void UMaterialExpressionVrrToonShadow::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Vrr Toon Shadow"));
}
#endif

#undef LOCTEXT_NAMESPACE
