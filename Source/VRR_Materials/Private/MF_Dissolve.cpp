#include "MF_Dissolve.h"

#include "MaterialCompiler.h"

#define LOCTEXT_NAMESPACE "VrrMaterialExpressions"

UMaterialExpressionVrrDissolve::UMaterialExpressionVrrDissolve(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(LOCTEXT("VRR", "VRR"));

	// Output 0: Opacity Mask (0/1). Output 1: edge glow for Emissive. Output 2: 0-1 edge mask.
	bShowOutputNameOnPin = true;
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Opacity Mask")));
	Outputs.Add(FExpressionOutput(TEXT("Edge Emissive")));
	Outputs.Add(FExpressionOutput(TEXT("Edge Mask")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrDissolve::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Noise.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Vrr Dissolve: missing Noise input"));
	}

	const int32 NoiseArg = Noise.Compile(Compiler);
	const int32 AmountArg = AmountIn.GetTracedInput().Expression ? AmountIn.Compile(Compiler) : Compiler->Constant(Amount);
	const int32 WidthArg = Compiler->Max(
		EdgeWidthIn.GetTracedInput().Expression ? EdgeWidthIn.Compile(Compiler) : Compiler->Constant(EdgeWidth),
		Compiler->Constant(0.001f));

	// Threshold runs from -EdgeWidth (nothing gone, edge hidden) to 1 (everything gone)
	const int32 One = Compiler->Constant(1.0f);
	const int32 Threshold = Compiler->Sub(Compiler->Mul(AmountArg, Compiler->Add(One, WidthArg)), WidthArg);

	const int32 Mask = Compiler->Step(Threshold, NoiseArg);
	if (OutputIndex == 0)
	{
		return Mask;
	}

	const int32 Distance = Compiler->Div(Compiler->Sub(NoiseArg, Threshold), WidthArg);
	const int32 Edge = Compiler->Mul(Compiler->Sub(One, Compiler->Saturate(Distance)), Mask);
	if (OutputIndex == 2)
	{
		return Edge;
	}

	const int32 ColorArg = EdgeColorIn.GetTracedInput().Expression ? EdgeColorIn.Compile(Compiler) : Compiler->Constant3(EdgeColor.R, EdgeColor.G, EdgeColor.B);
	const int32 IntensityArg = EdgeIntensityIn.GetTracedInput().Expression ? EdgeIntensityIn.Compile(Compiler) : Compiler->Constant(EdgeIntensity);

	return Compiler->Mul(ColorArg, Compiler->Mul(IntensityArg, Edge));
}

void UMaterialExpressionVrrDissolve::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Vrr Dissolve"));
}
#endif

#undef LOCTEXT_NAMESPACE
