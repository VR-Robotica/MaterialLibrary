#include "MF_TapeSignal.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

#define LOCTEXT_NAMESPACE "VrrMaterialExpressions"

namespace
{
	// RGB <-> YIQ (NTSC): Y is brightness, I and Q are color
	int32 ToYIQ(FMaterialCompiler* Compiler, int32 RGB)
	{
		const int32 Y = Compiler->Dot(RGB, Compiler->Constant3(0.299f, 0.587f, 0.114f));
		const int32 I = Compiler->Dot(RGB, Compiler->Constant3(0.596f, -0.274f, -0.322f));
		const int32 Q = Compiler->Dot(RGB, Compiler->Constant3(0.211f, -0.523f, 0.312f));
		return Compiler->AppendVector(Compiler->AppendVector(Y, I), Q);
	}

	int32 ToRGB(FMaterialCompiler* Compiler, int32 YIQ)
	{
		const int32 R = Compiler->Dot(YIQ, Compiler->Constant3(1.0f, 0.956f, 0.621f));
		const int32 G = Compiler->Dot(YIQ, Compiler->Constant3(1.0f, -0.272f, -0.647f));
		const int32 B = Compiler->Dot(YIQ, Compiler->Constant3(1.0f, -1.106f, 1.703f));
		return Compiler->AppendVector(Compiler->AppendVector(R, G), B);
	}
}

UMaterialExpressionVrrTapeSignal::UMaterialExpressionVrrTapeSignal(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(LOCTEXT("VRR", "VRR"));

	bShowOutputNameOnPin = true;
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Color")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrTapeSignal::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	if (!Color.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Vrr Tape Signal: missing Color input"));
	}
	const int32 One = Compiler->Constant(1.0f);
	const int32 ColorArg = Compiler->ValidCast(Color.Compile(Compiler), MCT_Float3);
	const int32 BaseUV = ViewportUV(Compiler, UV);
	const int32 Pixel = PixelAt1080(Compiler);
	const int32 GlitchArg = Glitch.GetTracedInput().Expression ? Compiler->ValidCast(Glitch.Compile(Compiler), MCT_Float1) : Compiler->Constant(0.0f);

	// 1. Chroma bleed: Y from Color, IQ from 5 scene taps spread over the bleed width, centered ChromaShift to the right
	int32 YIQ = ToYIQ(Compiler, ColorArg);
	const int32 YArg = X(Compiler, YIQ);
	int32 IQ = Compiler->ComponentMask(YIQ, false, true, true, false);
	if (ChromaBleedWidth > 0.0f)
	{
		constexpr int32 NumTaps = 5;
		int32 Sum = Compiler->Constant2(0.0f, 0.0f);
		for (int32 Tap = 0; Tap < NumTaps; ++Tap)
		{
			const float OffsetPx = ChromaShift + ChromaBleedWidth * (static_cast<float>(Tap) / (NumTaps - 1) - 0.5f);
			const int32 TapUV = Compiler->Add(BaseUV, Compiler->Mul(Pixel, Compiler->Constant2(OffsetPx, 0.0f)));
			Sum = Compiler->Add(Sum, Compiler->ComponentMask(ToYIQ(Compiler, SampleScene(Compiler, TapUV)), false, true, true, false));
		}
		const int32 Smeared = Compiler->Mul(Sum, Compiler->Constant(1.0f / NumTaps));
		IQ = Compiler->Lerp(IQ, Smeared, InputOr(Compiler, ChromaBleedIn, ChromaBleed));
	}
	IQ = Compiler->Mul(IQ, Compiler->Constant(Saturation));
	int32 Result = ToRGB(Compiler, Compiler->AppendVector(YArg, IQ));

	// 2. Scanlines: cos wave per line; gaps shrink on bright pixels
	const int32 V = Y(Compiler, BaseUV);
	const int32 ScanlineIntensityArg = InputOr(Compiler, ScanlineIntensityIn, ScanlineIntensity);
	{
		int32 LinePos = Compiler->Mul(V, Compiler->Constant(ScanlineCount));
		if (ScanlineRoll != 0.0f)
		{
			LinePos = Compiler->Add(LinePos, Compiler->Mul(Seconds(Compiler, bIgnorePause), Compiler->Constant(ScanlineRoll)));
		}
		const int32 Wave = Compiler->Add(Compiler->Constant(0.5f), Compiler->Mul(Compiler->Constant(0.5f), Compiler->Cosine(Compiler->Mul(LinePos, Compiler->Constant(UE_TWO_PI)))));
		const int32 Depth = Compiler->Mul(ScanlineIntensityArg, Compiler->Sub(One, Compiler->Mul(Compiler->Saturate(YArg), Compiler->Constant(0.5f))));
		Result = Compiler->Mul(Result, Compiler->Sub(One, Compiler->Mul(Depth, Compiler->Sub(One, Wave))));
	}

	// 3. Static: a random value per 1080p pixel per frame, heavier in glitch bands
	const int32 Seed = FrameSeed(Compiler, SteppedFrame(Compiler, FrameRate, bIgnorePause));
	{
		const int32 Cell = Compiler->Floor(Compiler->Div(BaseUV, Pixel));
		const int32 Noise = Compiler->Sub(Hash12(Compiler, Compiler->Add(Cell, Seed)), Compiler->Constant(0.5f));
		const int32 Amount = Compiler->Add(InputOr(Compiler, StaticIn, Static), Compiler->Mul(GlitchArg, Compiler->Constant(GlitchStatic)));
		Result = Compiler->Add(Result, Compiler->Mul(Noise, Compiler->Mul(Amount, Compiler->Constant(2.0f))));
	}

	// 4. Dropouts: on a few lines, a white streak from a random start that fades over its length
	if (Dropouts > 0.0f || Glitch.GetTracedInput().Expression)
	{
		const int32 Line = Compiler->Floor(Compiler->Mul(V, Compiler->Constant(ScanlineCount)));
		const int32 Rate = Compiler->Add(Compiler->Constant(Dropouts), Compiler->Mul(GlitchArg, Compiler->Constant(0.3f)));
		const int32 On = Compiler->Step(Hash2(Compiler, Line, Seed, 3.1f), Rate);
		const int32 Start = Hash2(Compiler, Line, Seed, 8.7f);
		const int32 Length = Compiler->Add(Compiler->Constant(0.05f), Compiler->Mul(Hash2(Compiler, Line, Seed, 1.9f), Compiler->Constant(0.25f)));
		const int32 Along = Compiler->Div(Compiler->Sub(X(Compiler, BaseUV), Start), Length);
		const int32 Inside = Compiler->Mul(Compiler->Step(Compiler->Constant(0.0f), Along), Compiler->Step(Along, One));
		const int32 Streak = Compiler->Mul(Compiler->Mul(On, Inside), Compiler->Sub(One, Compiler->Saturate(Along)));
		Result = Compiler->Add(Result, Compiler->Mul(Streak, Compiler->Constant(DropoutIntensity)));
	}

	return Compiler->Max(Result, Compiler->Constant(0.0f));
}

void UMaterialExpressionVrrTapeSignal::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Vrr Tape Signal"));
}
#endif

#undef LOCTEXT_NAMESPACE
