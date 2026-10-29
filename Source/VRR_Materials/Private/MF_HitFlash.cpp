/*
 * Vrr Hit Flash: an emissive flash that fades out after a hit.
 *
 * Strategy
 *  Gameplay code only writes the time of the hit into a scalar parameter (World->TimeSeconds). The shader works out
 *  how long ago that was from the game time, so no per-frame updates from code are needed while it fades.
 *  The fade is squared: it drops quickly at first and leaves a soft tail, which reads as a punchier flash.
 *
 * Outputs
 *  0 Emissive  Color * Intensity * Alpha
 *  1 Alpha     0-1 fade (1 at the hit, 0 after Duration)
 */

#include "MF_HitFlash.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

namespace VrrHitFlash
{
	enum EOutput : int32 { Emissive = 0, Alpha = 1 };
}

UMaterialExpressionVrrHitFlash::UMaterialExpressionVrrHitFlash(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Hit Flash"), { { TEXT("Emissive"), MCT_Float3 }, { TEXT("Alpha"), MCT_Float1 } });
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrHitFlash::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	if (!IsConnected(HitTime))
	{
		return MissingInput(Compiler, TEXT("HitTime"));
	}

	// 1. Seconds since the hit (game time, so the fade pauses with the game)
	const int32 Elapsed = Compiler->Sub(Compiler->GameTime(false, 0.0f), HitTime.Compile(Compiler));
	const int32 SafeDuration = Compiler->Max(InputOr(Compiler, DurationIn, Duration), Compiler->Constant(0.01f));

	// 2. Alpha = saturate(1 - elapsed / duration), squared for a softer tail
	const int32 Linear = Compiler->Saturate(Compiler->Sub(Compiler->Constant(1.0f), Compiler->Div(Elapsed, SafeDuration)));
	const int32 Alpha = Compiler->Mul(Linear, Linear);
	if (OutputIndex == VrrHitFlash::Alpha)
	{
		return Alpha;
	}

	// 3. Flash color, scaled by intensity and the fade
	const int32 ColorArg = ColorOr(Compiler, ColorIn, Color);
	const int32 IntensityArg = InputOr(Compiler, IntensityIn, Intensity);
	return Compiler->Mul(ColorArg, Compiler->Mul(IntensityArg, Alpha));
}
#endif
