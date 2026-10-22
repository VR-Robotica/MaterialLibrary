#include "MF_ToonShading.h"

#include "MaterialCompiler.h"

#define LOCTEXT_NAMESPACE "VrrMaterialExpressions"

UMaterialExpressionVrrToonShading::UMaterialExpressionVrrToonShading(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(LOCTEXT("VRR", "VRR"));

	// Output 0: final color for an Unlit material's Emissive. Outputs 1-3: 0-1 masks for custom mixing.
	bShowOutputNameOnPin = true;
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Emissive")));
	Outputs.Add(FExpressionOutput(TEXT("Light Mask")));
	Outputs.Add(FExpressionOutput(TEXT("Specular Mask")));
	Outputs.Add(FExpressionOutput(TEXT("Rim Mask")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrToonShading::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	auto CompileScalar = [Compiler](FExpressionInput& Input, float Default)
	{
		return Input.GetTracedInput().Expression ? Input.Compile(Compiler) : Compiler->Constant(Default);
	};

	// Vector parameters (e.g. from a Parameter Collection) are float4, so drop the alpha
	auto CompileVector = [Compiler](FExpressionInput& Input, const FVector3f& Default)
	{
		if (!Input.GetTracedInput().Expression)
		{
			return Compiler->Constant3(Default.X, Default.Y, Default.Z);
		}
		const int32 Arg = Input.Compile(Compiler);
		if (Arg != INDEX_NONE && Compiler->GetType(Arg) == MCT_Float4)
		{
			return Compiler->ComponentMask(Arg, true, true, true, false);
		}
		return Arg;
	};
	auto ColorToVector = [](const FLinearColor& Color) { return FVector3f(Color.R, Color.G, Color.B); };

	const int32 One = Compiler->Constant(1.0f);
	const int32 MinSoftness = Compiler->Constant(0.0005f);

	// Vectors: surface normal, direction to the fake light, direction to the camera
	int32 N;
	if (Normal.GetTracedInput().Expression)
	{
		N = CompileVector(Normal, FVector3f::UnitZ());
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
	const int32 L = Compiler->Normalize(CompileVector(LightDirectionIn, FVector3f(LightDirection)));
	const int32 V = Compiler->CameraVector();
	const int32 NdotL = Compiler->Dot(N, L);

	// Light mask: lit part (NdotL above Threshold) remapped to 0-1, then split into bands with a soft edge
	const int32 ThresholdArg = CompileScalar(ThresholdIn, Threshold);
	const int32 SoftnessArg = Compiler->Max(CompileScalar(SoftnessIn, Softness), MinSoftness);
	const int32 BandsArg = Compiler->Max(Compiler->Floor(CompileScalar(BandsIn, Bands)), One);

	const int32 LitRange = Compiler->Max(Compiler->Sub(One, ThresholdArg), Compiler->Constant(0.001f));
	const int32 BandPos = Compiler->Mul(Compiler->Saturate(Compiler->Div(Compiler->Sub(NdotL, ThresholdArg), LitRange)), BandsArg);
	const int32 EdgeWidth = Compiler->Div(Compiler->Mul(SoftnessArg, BandsArg), LitRange);
	const int32 BandStep = Compiler->Saturate(Compiler->Div(Compiler->Frac(BandPos), EdgeWidth));
	int32 LightMask = Compiler->Div(Compiler->Add(Compiler->Floor(BandPos), BandStep), BandsArg);
	if (Shadow.GetTracedInput().Expression)
	{
		LightMask = Compiler->Mul(LightMask, Compiler->Saturate(Shadow.Compile(Compiler)));
	}
	if (OutputIndex == 1)
	{
		return LightMask;
	}

	// Specular: hard-edged highlight where the half vector lines up with the normal
	const int32 H = Compiler->Normalize(Compiler->Add(L, V));
	const int32 NdotH = Compiler->Dot(N, H);
	const int32 SpecEdge = Compiler->Sub(One, CompileScalar(SpecularSizeIn, SpecularSize));
	const int32 SpecSoftness = Compiler->Max(Compiler->Mul(SoftnessArg, Compiler->Constant(0.5f)), MinSoftness);
	const int32 SpecularMask = Compiler->Mul(
		Compiler->SmoothStep(Compiler->Sub(SpecEdge, SpecSoftness), Compiler->Add(SpecEdge, SpecSoftness), NdotH),
		LightMask);
	if (OutputIndex == 2)
	{
		return SpecularMask;
	}

	// Rim: band along the silhouette, optionally faded out on the shadow side
	const int32 Fresnel = Compiler->Sub(One, Compiler->Saturate(Compiler->Dot(N, V)));
	const int32 RimEdge = Compiler->Sub(One, CompileScalar(RimWidthIn, RimWidth));
	const int32 RimShape = Compiler->SmoothStep(Compiler->Sub(RimEdge, SoftnessArg), Compiler->Add(RimEdge, SoftnessArg), Fresnel);
	const int32 RimMask = Compiler->Mul(RimShape, Compiler->Lerp(One, LightMask, Compiler->Constant(RimLightAlign)));
	if (OutputIndex == 3)
	{
		return RimMask;
	}

	// Final color
	const int32 BaseColorArg = CompileVector(BaseColorIn, ColorToVector(BaseColor));
	const int32 LightColorArg = CompileVector(LightColorIn, ColorToVector(LightColor));
	const int32 ShadowColorArg = CompileVector(ShadowColorIn, ColorToVector(ShadowColor));
	const int32 RimColorArg = CompileVector(RimColorIn, ColorToVector(RimColor));

	const int32 Diffuse = Compiler->Mul(BaseColorArg, Compiler->Lerp(ShadowColorArg, LightColorArg, LightMask));
	const int32 Specular = Compiler->Mul(LightColorArg, Compiler->Mul(SpecularMask, CompileScalar(SpecularIntensityIn, SpecularIntensity)));
	const int32 Rim = Compiler->Mul(RimColorArg, Compiler->Mul(RimMask, CompileScalar(RimIntensityIn, RimIntensity)));

	return Compiler->Add(Diffuse, Compiler->Add(Specular, Rim));
}

void UMaterialExpressionVrrToonShading::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Vrr Toon Shading"));
}
#endif

#undef LOCTEXT_NAMESPACE
