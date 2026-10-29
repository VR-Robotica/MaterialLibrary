/*
 * Vrr Film Grain: soft, clumpy film grain for a Post Process material.
 *
 * Strategy
 *  Real grain is clumps of silver crystals, not per-pixel static, so the grain is smooth value noise on a grid of
 *  Size-pixel cells (measured at 1080p, so it looks the same at any resolution). Film shows a new frame 24 times a
 *  second, so the noise pattern only changes on whole frames: each frame shifts the noise by a pseudo-random offset.
 *  Emulsion grain shows most in the midtones, so the grain is weighted by 4L(1-L), which is 1 at mid gray and 0 at
 *  pure black and white.
 *
 * Steps
 *  1. Grain position: viewport UV in grain cells.
 *  2. Frame offset: a new random shift per film frame (Frame pin or Frame Rate).
 *  3. Signed grain -1..1 from value noise (one value, or one per channel when Colored).
 *  4. Midtone weight, then Color + Grain * Amount * Weight.
 *
 * Outputs
 *  0 Color  the picture with grain
 *  1 Grain  the signed -1..1 grain on its own
 */

#include "MF_FilmGrain.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

namespace VrrFilmGrain
{
	enum EOutput : int32 { Color = 0, Grain = 1 };
}

UMaterialExpressionVrrFilmGrain::UMaterialExpressionVrrFilmGrain(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Film Grain"), { TEXT("Color"), TEXT("Grain") });
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrFilmGrain::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	if (!IsConnected(Color))
	{
		return MissingInput(Compiler, TEXT("Color"));
	}
	const int32 One = Compiler->Constant(1.0f);

	// RGB (a scalar is spread to gray, alpha is dropped)
	const int32 ColorArg = Compiler->ValidCast(Color.Compile(Compiler), MCT_Float3);

	// 1. Grain cells: 1080p pixels divided by Size, so the grain looks the same at any resolution
	const int32 CellSize = Compiler->Mul(PixelAt1080(Compiler), InputOr(Compiler, SizeIn, Size));
	const int32 GrainPos = Compiler->Div(ViewportUV(Compiler, UV), CellSize);

	// 2. A new pattern per film frame: shift by a pseudo-random amount, kept small for float precision
	const int32 FrameArg = IsConnected(Frame) ? Frame.Compile(Compiler) : SteppedFrame(Compiler, FrameRate, bIgnorePause);
	const int32 FrameOffset = Compiler->Mul(FrameRandom(Compiler, FrameArg), Compiler->Constant2(1013.0f, 787.0f));
	const int32 Base = Compiler->Add(GrainPos, FrameOffset);

	// 3. Signed grain -1..1. Colored film gets an independent grain per channel (different seeds).
	auto SignedGrain = [&](float SeedX, float SeedY)
	{
		return ToSigned(Compiler, ValueNoise(Compiler, Compiler->Add(Base, Compiler->Constant2(SeedX, SeedY))));
	};
	const int32 GrainArg = bColored
		? Compiler->AppendVector(Compiler->AppendVector(SignedGrain(0.0f, 0.0f), SignedGrain(57.3f, 11.9f)), SignedGrain(23.1f, 91.7f))
		: SignedGrain(0.0f, 0.0f);
	if (OutputIndex == VrrFilmGrain::Grain)
	{
		return GrainArg;
	}

	// 4. Strongest in the midtones: Weight = lerp(1, 4L(1-L), MidtoneResponse)
	const int32 L = Compiler->Saturate(Luminance(Compiler, ColorArg));
	const int32 Midtone = Compiler->Mul(Compiler->Mul(Compiler->Constant(4.0f), L), Compiler->Sub(One, L));
	const int32 Weight = Compiler->Lerp(One, Midtone, Compiler->Constant(MidtoneResponse));

	return Compiler->Add(ColorArg, Compiler->Mul(GrainArg, Compiler->Mul(InputOr(Compiler, AmountIn, Amount), Weight)));
}
#endif
