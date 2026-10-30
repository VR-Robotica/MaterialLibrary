/*
 * Vrr Dissolve: noise-driven dissolve with a glowing burn edge.
 *
 * Strategy
 *  A pixel disappears once Amount climbs past its Noise value, so bright noise survives longest. The threshold
 *  starts at -EdgeWidth (not 0) so the edge band is fully hidden at Amount 0, and reaches 1 so everything is gone
 *  at Amount 1. The edge is the strip of noise values just above the threshold, fading from 1 at the cut to 0
 *  one EdgeWidth away.
 *
 * Outputs
 *  0 Opacity Mask   0/1, for a Masked material's Opacity Mask
 *  1 Edge Emissive  EdgeColor * EdgeIntensity * Edge, for Emissive
 *  2 Edge Mask      the 0-1 edge strip, for custom mixing
 */

#include "MF_Dissolve.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

namespace VrrDissolve
{
	enum EOutput : int32 { OpacityMask = 0, EdgeEmissive = 1, EdgeMask = 2 };
}

UMaterialExpressionVrrDissolve::UMaterialExpressionVrrDissolve(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Dissolve"), {
		{ TEXT("Opacity Mask"), MCT_Float1 },
		{ TEXT("Edge Emissive"), MCT_Float3 },
		{ TEXT("Edge Mask"), MCT_Float1 }
	});
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrDissolve::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	if (!IsConnected(Noise))
	{
		return MissingInput(Compiler, TEXT("Noise"));
	}
	const int32 One = Compiler->Constant(1.0f);

	// 1. Inputs (pins override the properties)
	const int32 NoiseArg = Noise.Compile(Compiler);
	const int32 AmountArg = InputOr(Compiler, AmountIn, Amount);
	const int32 WidthArg = Compiler->Max(InputOr(Compiler, EdgeWidthIn, EdgeWidth), Compiler->Constant(0.001f));

	// 2. Threshold runs from -EdgeWidth (nothing gone, edge hidden) to 1 (everything gone):
	//    Threshold = Amount * (1 + EdgeWidth) - EdgeWidth
	const int32 Threshold = Compiler->Sub(Compiler->Mul(AmountArg, Compiler->Add(One, WidthArg)), WidthArg);

	// 3. Visible where the noise is still above the threshold
	const int32 Mask = Compiler->Step(Threshold, NoiseArg);
	if (OutputIndex == VrrDissolve::OpacityMask)
	{
		return Mask;
	}

	// 4. Edge: 1 right at the cut, fading to 0 one EdgeWidth above it (only on visible pixels)
	const int32 Distance = Compiler->Div(Compiler->Sub(NoiseArg, Threshold), WidthArg);
	const int32 Edge = Compiler->Mul(Compiler->Sub(One, Compiler->Saturate(Distance)), Mask);
	if (OutputIndex == VrrDissolve::EdgeMask)
	{
		return Edge;
	}

	// 5. Glow: tint and brighten the edge
	const int32 ColorArg = ColorOr(Compiler, EdgeColorIn, EdgeColor);
	const int32 IntensityArg = InputOr(Compiler, EdgeIntensityIn, EdgeIntensity);
	return Compiler->Mul(ColorArg, Compiler->Mul(IntensityArg, Edge));
}
#endif
