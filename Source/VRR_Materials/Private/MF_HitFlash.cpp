#include "MF_HitFlash.h"

#include "MaterialCompiler.h"

#define LOCTEXT_NAMESPACE "VrrMaterialExpressions"

UMaterialExpressionVrrHitFlash::UMaterialExpressionVrrHitFlash(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(LOCTEXT("VRR", "VRR"));

	// Output 0: flash color for Emissive. Output 1: 0-1 fade mask.
	bShowOutputNameOnPin = true;
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Emissive")));
	Outputs.Add(FExpressionOutput(TEXT("Alpha")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrHitFlash::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!HitTime.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Vrr Hit Flash: missing HitTime input"));
	}

	const int32 HitTimeArg = HitTime.Compile(Compiler);
	const int32 DurationArg = DurationIn.GetTracedInput().Expression ? DurationIn.Compile(Compiler) : Compiler->Constant(Duration);

	// Alpha = saturate(1 - elapsed / duration), squared for a softer tail
	const int32 Elapsed = Compiler->Sub(Compiler->GameTime(false, 0.0f), HitTimeArg);
	const int32 SafeDuration = Compiler->Max(DurationArg, Compiler->Constant(0.01f));
	const int32 Linear = Compiler->Saturate(Compiler->Sub(Compiler->Constant(1.0f), Compiler->Div(Elapsed, SafeDuration)));
	const int32 Alpha = Compiler->Mul(Linear, Linear);

	if (OutputIndex == 1)
	{
		return Alpha;
	}

	const int32 ColorArg = ColorIn.GetTracedInput().Expression ? ColorIn.Compile(Compiler) : Compiler->Constant3(Color.R, Color.G, Color.B);
	const int32 IntensityArg = IntensityIn.GetTracedInput().Expression ? IntensityIn.Compile(Compiler) : Compiler->Constant(Intensity);

	return Compiler->Mul(ColorArg, Compiler->Mul(IntensityArg, Alpha));
}

void UMaterialExpressionVrrHitFlash::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Vrr Hit Flash"));
}
#endif

#undef LOCTEXT_NAMESPACE
