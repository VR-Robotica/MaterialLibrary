#include "MF_TapeWarp.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

#define LOCTEXT_NAMESPACE "VrrMaterialExpressions"

UMaterialExpressionVrrTapeWarp::UMaterialExpressionVrrTapeWarp(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(LOCTEXT("VRR", "VRR"));

	bShowOutputNameOnPin = true;
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("UV")));
	Outputs.Add(FExpressionOutput(TEXT("Glitch")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrTapeWarp::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	const int32 BaseUV = ViewportUV(Compiler, UV);
	const int32 V = Y(Compiler, BaseUV);
	const int32 One = Compiler->Constant(1.0f);
	const int32 Two = Compiler->Constant(2.0f);
	auto Signed = [&](int32 Value01) { return Compiler->Sub(Compiler->Mul(Value01, Two), One); };

	const int32 Time = Seconds(Compiler, bIgnorePause);
	const int32 Seed = FrameSeed(Compiler, SteppedFrame(Compiler, FrameRate, bIgnorePause));
	const int32 Line = Compiler->Floor(Compiler->Mul(V, Compiler->Constant(FMath::Max(LineCount, 1.0f))));

	// Glitch band: each window (1 / GlitchRate seconds) may have one, rolling down from a random start
	const int32 Window = Compiler->Mul(Time, Compiler->Constant(FMath::Max(GlitchRate, 0.05f)));
	const int32 WindowIndex = Compiler->Floor(Window);
	const int32 Active = Compiler->Step(Hash2(Compiler, WindowIndex, Compiler->Constant(0.0f), 7.13f), InputOr(Compiler, GlitchChanceIn, GlitchChance));
	const int32 WindowSeconds = Compiler->Div(Compiler->Frac(Window), Compiler->Constant(FMath::Max(GlitchRate, 0.05f)));
	const int32 BandCenter = Compiler->Frac(Compiler->Add(Hash2(Compiler, WindowIndex, Compiler->Constant(0.0f), 3.71f),
		Compiler->Mul(WindowSeconds, Compiler->Constant(GlitchSpeed))));
	// 0 at the band edge, 1 in its middle, squared for a soft falloff
	const int32 BandDistance = Compiler->Div(Compiler->Abs(Compiler->Sub(V, BandCenter)), Compiler->Constant(FMath::Max(GlitchHeight, 0.005f) * 0.5f));
	const int32 BandEdge = Compiler->Saturate(Compiler->Sub(One, BandDistance));
	const int32 Glitch = Compiler->Mul(Active, Compiler->Mul(BandEdge, BandEdge));
	if (OutputIndex == 1)
	{
		return Glitch;
	}

	// Sideways offset in 1080p pixels: jitter + wobble + glitch tear + head switching
	int32 Offset = Compiler->Mul(Signed(Hash2(Compiler, Line, Seed, 0.0f)), InputOr(Compiler, LineJitterIn, LineJitter));
	if (WobbleAmount > 0.0f)
	{
		const int32 Phase = Compiler->Add(Compiler->Mul(V, Compiler->Constant(WobbleFrequency * UE_TWO_PI)), Compiler->Mul(Time, Compiler->Constant(WobbleSpeed)));
		Offset = Compiler->Add(Offset, Compiler->Mul(Compiler->Sine(Phase), Compiler->Constant(WobbleAmount)));
	}
	const int32 Tear = Compiler->Mul(Signed(Hash2(Compiler, Line, Seed, 13.7f)), InputOr(Compiler, GlitchStrengthIn, GlitchStrength));
	Offset = Compiler->Add(Offset, Compiler->Mul(Tear, Glitch));
	if (HeadSwitchHeight > 0.0f)
	{
		// 0 above the strip, 1 on the bottom line (viewport V grows downward)
		const int32 Strip = Compiler->Saturate(Compiler->Div(Compiler->Sub(V, Compiler->Constant(1.0f - HeadSwitchHeight)), Compiler->Constant(HeadSwitchHeight)));
		const int32 Wiggle = Compiler->Add(Compiler->Constant(0.6f), Compiler->Mul(Hash2(Compiler, Line, Seed, 5.3f), Compiler->Constant(0.4f)));
		Offset = Compiler->Add(Offset, Compiler->Mul(Compiler->Mul(Compiler->Mul(Strip, Strip), Wiggle), InputOr(Compiler, HeadSwitchShiftIn, HeadSwitchShift)));
	}

	const int32 OffsetUV = Compiler->AppendVector(Compiler->Mul(Offset, X(Compiler, PixelAt1080(Compiler))), Compiler->Constant(0.0f));
	return Compiler->Add(BaseUV, OffsetUV);
}

void UMaterialExpressionVrrTapeWarp::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Vrr Tape Warp"));
}
#endif

#undef LOCTEXT_NAMESPACE
