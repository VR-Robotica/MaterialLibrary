#include "MF_Halation.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

#define LOCTEXT_NAMESPACE "VrrMaterialExpressions"

UMaterialExpressionVrrHalation::UMaterialExpressionVrrHalation(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(LOCTEXT("VRR", "VRR"));

	bShowOutputNameOnPin = true;
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Glow")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrHalation::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	const int32 BaseUV = ViewportUV(Compiler, UV);
	const int32 ThresholdArg = InputOr(Compiler, ThresholdIn, Threshold);
	const int32 KneeArg = Compiler->Constant(FMath::Max(Knee, 0.001f));
	auto Bright = [&](int32 Color)
	{
		return Compiler->Saturate(Compiler->Div(Compiler->Sub(Luminance(Compiler, Color), ThresholdArg), KneeArg));
	};

	// Disk radius in UV
	const int32 RadiusUV = Compiler->Mul(PixelAt1080(Compiler), InputOr(Compiler, RadiusIn, Radius));

	// Per-pixel rotation of the pattern (cos, sin)
	int32 Cos = INDEX_NONE;
	int32 Sin = INDEX_NONE;
	if (bJitter)
	{
		const int32 Pixel = Compiler->Floor(Compiler->Mul(BaseUV, ViewSize(Compiler)));
		const int32 Angle = Compiler->Mul(Hash12(Compiler, Pixel), Compiler->Constant(UE_TWO_PI));
		Cos = Compiler->Cosine(Angle);
		Sin = Compiler->Sine(Angle);
	}

	// Golden-angle spiral over the unit disk (even coverage), nearer taps weigh more
	const int32 NumTaps = FMath::Clamp(Samples, 4, 64);
	int32 Sum = Compiler->Constant(0.0f);
	float TotalWeight = 0.0f;
	for (int32 Index = 0; Index < NumTaps; ++Index)
	{
		const float R = FMath::Sqrt((Index + 0.5f) / NumTaps);
		const float Angle = Index * 2.39996323f;
		const float TapX = R * FMath::Cos(Angle);
		const float TapY = R * FMath::Sin(Angle);
		const float Weight = 1.0f - 0.75f * R;

		int32 Offset;
		if (bJitter)
		{
			// (x cos - y sin, x sin + y cos)
			const int32 RX = Compiler->Sub(Compiler->Mul(Compiler->Constant(TapX), Cos), Compiler->Mul(Compiler->Constant(TapY), Sin));
			const int32 RY = Compiler->Add(Compiler->Mul(Compiler->Constant(TapX), Sin), Compiler->Mul(Compiler->Constant(TapY), Cos));
			Offset = Compiler->AppendVector(RX, RY);
		}
		else
		{
			Offset = Compiler->Constant2(TapX, TapY);
		}
		const int32 Tap = SampleScene(Compiler, Compiler->Add(BaseUV, Compiler->Mul(Offset, RadiusUV)));
		Sum = Compiler->Add(Sum, Compiler->Mul(Bright(Tap), Compiler->Constant(Weight)));
		TotalWeight += Weight;
	}
	int32 Halo = Compiler->Mul(Sum, Compiler->Constant(1.0f / TotalWeight));

	if (bKeepHighlightsClean)
	{
		Halo = Compiler->Mul(Halo, Compiler->Sub(Compiler->Constant(1.0f), Bright(SampleScene(Compiler, BaseUV))));
	}

	const int32 TintArg = TintIn.GetTracedInput().Expression ? TintIn.Compile(Compiler) : Compiler->Constant3(Tint.R, Tint.G, Tint.B);
	return Compiler->Mul(Compiler->Mul(TintArg, InputOr(Compiler, IntensityIn, Intensity)), Halo);
}

void UMaterialExpressionVrrHalation::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Vrr Halation"));
}
#endif

#undef LOCTEXT_NAMESPACE
