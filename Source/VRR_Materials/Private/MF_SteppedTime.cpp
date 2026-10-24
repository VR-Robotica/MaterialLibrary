#include "MF_SteppedTime.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

#define LOCTEXT_NAMESPACE "VrrMaterialExpressions"

UMaterialExpressionVrrSteppedTime::UMaterialExpressionVrrSteppedTime(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(LOCTEXT("VRR", "VRR"));

	bShowOutputNameOnPin = true;
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Time")));
	Outputs.Add(FExpressionOutput(TEXT("Frame")));
	Outputs.Add(FExpressionOutput(TEXT("Seed")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrSteppedTime::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	const int32 Time = TimeIn.GetTracedInput().Expression ? TimeIn.Compile(Compiler)
		: bIgnorePause ? Compiler->RealTime(false, 0.0f) : Compiler->GameTime(false, 0.0f);
	const int32 Rate = Compiler->Max(VrrMaterialMath::InputOr(Compiler, FrameRateIn, FrameRate), Compiler->Constant(0.1f));
	const int32 PhaseArg = VrrMaterialMath::InputOr(Compiler, PhaseIn, Phase);

	const int32 Frame = Compiler->Floor(Compiler->Add(Compiler->Mul(Time, Rate), PhaseArg));
	switch (OutputIndex)
	{
	case 1:
		return Frame;
	case 2:
		return Compiler->Frac(Compiler->Mul(Frame, Compiler->Constant(0.6180339887f)));
	default:
		return Compiler->Div(Compiler->Sub(Frame, PhaseArg), Rate);
	}
}

void UMaterialExpressionVrrSteppedTime::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Vrr Stepped Time"));
}
#endif

#undef LOCTEXT_NAMESPACE
