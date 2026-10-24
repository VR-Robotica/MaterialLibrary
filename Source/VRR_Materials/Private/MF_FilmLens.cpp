#include "MF_FilmLens.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

#define LOCTEXT_NAMESPACE "VrrMaterialExpressions"

UMaterialExpressionVrrFilmLens::UMaterialExpressionVrrFilmLens(const FObjectInitializer& ObjectInitializer)
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
int32 UMaterialExpressionVrrFilmLens::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	const int32 BaseUV = ViewportUV(Compiler, UV);
	const int32 Size = ViewSize(Compiler);

	// Unconnected and zero: skip that effect entirely
	const bool bChromatic = ChromaticAberrationIn.GetTracedInput().Expression || ChromaticAberration > 0.0f;
	const bool bSoft = SoftnessIn.GetTracedInput().Expression || Softness > 0.0f;

	// Chromatic offset: along the direction from the center, r = 0 at the center, 1 in the corners (aspect-corrected)
	int32 ChromaticOffset = INDEX_NONE;
	if (bChromatic)
	{
		const int32 FromCenter = Compiler->Sub(BaseUV, Compiler->Constant2(0.5f, 0.5f));
		const int32 Aspect = Compiler->AppendVector(Compiler->Div(X(Compiler, Size), Y(Compiler, Size)), Compiler->Constant(1.0f));
		const int32 CornerDistance = Compiler->Mul(Compiler->Length(Aspect), Compiler->Constant(0.5f));
		const int32 R = Compiler->Div(Compiler->Length(Compiler->Mul(FromCenter, Aspect)), CornerDistance);
		const int32 Amount = InputOr(Compiler, ChromaticAberrationIn, ChromaticAberration);
		const int32 Strength = Compiler->Power(Compiler->Max(R, Compiler->Constant(0.0001f)), Compiler->Constant(ChromaticFalloff));
		ChromaticOffset = Compiler->Mul(Compiler->Mul(FromCenter, Compiler->Constant(2.0f)), Compiler->Mul(Amount, Strength));
	}

	// One tap: RGB at UV, or R pushed out, G in place, B pulled in
	auto Tap = [&](int32 TapUV)
	{
		if (!bChromatic)
		{
			return SampleScene(Compiler, TapUV);
		}
		const int32 Red = X(Compiler, SampleScene(Compiler, Compiler->Add(TapUV, ChromaticOffset)));
		const int32 Green = Y(Compiler, SampleScene(Compiler, TapUV));
		const int32 Blue = Z(Compiler, SampleScene(Compiler, Compiler->Sub(TapUV, ChromaticOffset)));
		return Compiler->AppendVector(Compiler->AppendVector(Red, Green), Blue);
	};

	if (!bSoft)
	{
		return Tap(BaseUV);
	}

	// Softness: center 40%, four diagonal taps 15% each
	const int32 Radius = Compiler->Mul(PixelAt1080(Compiler), InputOr(Compiler, SoftnessIn, Softness));
	int32 Color = Compiler->Mul(Tap(BaseUV), Compiler->Constant(0.4f));
	for (const FVector2f Diagonal : { FVector2f(1, 1), FVector2f(-1, 1), FVector2f(1, -1), FVector2f(-1, -1) })
	{
		const int32 TapUV = Compiler->Add(BaseUV, Compiler->Mul(Radius, Compiler->Constant2(Diagonal.X, Diagonal.Y)));
		Color = Compiler->Add(Color, Compiler->Mul(Tap(TapUV), Compiler->Constant(0.15f)));
	}
	return Color;
}

void UMaterialExpressionVrrFilmLens::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Vrr Film Lens"));
}
#endif

#undef LOCTEXT_NAMESPACE
