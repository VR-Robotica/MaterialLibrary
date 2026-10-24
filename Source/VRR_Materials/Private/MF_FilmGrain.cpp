#include "MF_FilmGrain.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

#define LOCTEXT_NAMESPACE "VrrMaterialExpressions"

UMaterialExpressionVrrFilmGrain::UMaterialExpressionVrrFilmGrain(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(LOCTEXT("VRR", "VRR"));

	bShowOutputNameOnPin = true;
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Color")));
	Outputs.Add(FExpressionOutput(TEXT("Grain")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrFilmGrain::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	if (!Color.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Vrr Film Grain: missing Color input"));
	}
	// RGB (a scalar is spread to gray, alpha is dropped)
	const int32 ColorArg = Compiler->ValidCast(Color.Compile(Compiler), MCT_Float3);

	// Grain cells: pixels at 1080p divided by Size, so the grain looks the same at any resolution
	const int32 CellSize = Compiler->Mul(PixelAt1080(Compiler), InputOr(Compiler, SizeIn, Size));
	const int32 GrainPos = Compiler->Div(ViewportUV(Compiler, UV), CellSize);

	// A new pattern per frame: shift by a pseudo-random amount (golden ratio sequence), kept small for float precision
	int32 FrameArg;
	if (Frame.GetTracedInput().Expression)
	{
		FrameArg = Frame.Compile(Compiler);
	}
	else
	{
		const int32 Time = bIgnorePause ? Compiler->RealTime(false, 0.0f) : Compiler->GameTime(false, 0.0f);
		FrameArg = Compiler->Floor(Compiler->Mul(Time, Compiler->Constant(FMath::Max(FrameRate, 1.0f))));
	}
	const int32 FrameOffset = Compiler->Mul(Compiler->Frac(Compiler->Mul(FrameArg, Compiler->Constant(0.6180339887f))), Compiler->Constant2(1013.0f, 787.0f));
	const int32 Base = Compiler->Add(GrainPos, FrameOffset);

	// Signed grain: -1..1, one value or one per channel
	auto Grain = [&](float SeedX, float SeedY)
	{
		const int32 Noise = ValueNoise(Compiler, Compiler->Add(Base, Compiler->Constant2(SeedX, SeedY)));
		return Compiler->Sub(Compiler->Mul(Noise, Compiler->Constant(2.0f)), Compiler->Constant(1.0f));
	};
	const int32 GrainArg = bColored
		? Compiler->AppendVector(Compiler->AppendVector(Grain(0.0f, 0.0f), Grain(57.3f, 11.9f)), Grain(23.1f, 91.7f))
		: Grain(0.0f, 0.0f);
	if (OutputIndex == 1)
	{
		return GrainArg;
	}

	// Strongest in the midtones: lerp(1, 4L(1-L), Response)
	const int32 L = Compiler->Saturate(Luminance(Compiler, ColorArg));
	const int32 Midtone = Compiler->Mul(Compiler->Mul(Compiler->Constant(4.0f), L), Compiler->Sub(Compiler->Constant(1.0f), L));
	const int32 Weight = Compiler->Lerp(Compiler->Constant(1.0f), Midtone, Compiler->Constant(MidtoneResponse));

	const int32 AmountArg = InputOr(Compiler, AmountIn, Amount);
	return Compiler->Add(ColorArg, Compiler->Mul(GrainArg, Compiler->Mul(AmountArg, Weight)));
}

void UMaterialExpressionVrrFilmGrain::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Vrr Film Grain"));
}
#endif

#undef LOCTEXT_NAMESPACE
