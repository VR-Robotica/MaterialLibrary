// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Vrr Stepped Time: time that advances in held steps (animation "on twos" / "on threes").
 *
 * Strategy
 *  Count whole steps since time zero (Frame), then turn that back into seconds. Anything driven by the stepped
 *  seconds holds still for a step and then jumps, while the game keeps rendering at full frame rate.
 *  Phase shifts where the steps fall, so objects fed different phases (e.g. Per Instance Random) don't all jump on
 *  the same frame.
 *
 * Outputs
 *  0 Time   stepped seconds: (Frame - Phase) / FrameRate, never ahead of real time
 *  1 Frame  whole step count: floor(Time * FrameRate + Phase)
 *  2 Seed   0-1 random value that changes every step (for noise seeds)
 */

#include "MF_SteppedTime.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

namespace VrrSteppedTime
{
	enum EOutput : int32 { Time = 0, Frame = 1, Seed = 2 };
}

UMaterialExpressionVrrSteppedTime::UMaterialExpressionVrrSteppedTime(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Stepped Time"), {
		{ TEXT("Time"), MCT_Float1 },
		{ TEXT("Frame"), MCT_Float1 },
		{ TEXT("Seed"), MCT_Float1 }
	});
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrSteppedTime::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	// 1. Source time: the Time pin, else game time (or real time with Ignore Pause)
	const int32 Time = IsConnected(TimeIn) ? TimeIn.Compile(Compiler) : Seconds(Compiler, bIgnorePause);
	const int32 Rate = Compiler->Max(InputOr(Compiler, FrameRateIn, FrameRate), Compiler->Constant(0.1f));
	const int32 PhaseArg = InputOr(Compiler, PhaseIn, Phase);

	// 2. Whole steps so far, shifted by Phase
	const int32 Frame = Compiler->Floor(Compiler->Add(Compiler->Mul(Time, Rate), PhaseArg));

	switch (OutputIndex)
	{
	case VrrSteppedTime::Frame:
		return Frame;
	case VrrSteppedTime::Seed:
		// 3a. A new 0-1 value every step
		return FrameRandom(Compiler, Frame);
	default:
		// 3b. Back to seconds: the start time of the current step
		return Compiler->Div(Compiler->Sub(Frame, PhaseArg), Rate);
	}
}
#endif
