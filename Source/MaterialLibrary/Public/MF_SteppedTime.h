#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
#include "MF_SteppedTime.generated.h"

/**
 * Time that advances in steps, like hand-drawn animation "on twos" (12 fps) or "on threes" (8 fps),
 * while the game itself still renders at full frame rate. Feed it into anything animated by time
 * (panners, WPO wobble, VFX, outline noise) to get the held-frame look.
 *
 *   Frame = floor(Time * FrameRate + Phase)
 *   Time  = (Frame - Phase) / FrameRate     (stepped seconds, never ahead of real time)
 *   Seed  = frac(Frame * 0.618...)           (0-1, a new value every step, for noise seeds)
 *
 * Phase (0-1 of a step, e.g. Per Instance Random) lets objects change frame at different moments.
 */
UCLASS(collapsecategories, hidecategories=Object)
class MATERIALLIBRARY_API UMaterialExpressionVrrSteppedTime : public UMaterialExpressionVrrBase
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrSteppedTime(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(meta = (VrrType = "S", RequiredInput = "false", ToolTip = "Seconds to step. Defaults to the game time (or real time with 'Ignore Pause')"))
	FExpressionInput TimeIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Frame Rate' if not specified"))
	FExpressionInput FrameRateIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "0-1 offset within a step, so objects don't all change frame together (e.g. Per Instance Random). Defaults to 'Phase'"))
	FExpressionInput PhaseIn;

	/** Steps per second: 12 = on twos, 8 = on threes, 6 for slow effects. */
	UPROPERTY(EditAnywhere, Category = "Stepped Time", meta = (OverridingInputProperty = "FrameRateIn", ClampMin = "0.1", UIMax = "60.0"))
	float FrameRate = 12.0f;

	/** 0-1 offset within a step. */
	UPROPERTY(EditAnywhere, Category = "Stepped Time", meta = (OverridingInputProperty = "PhaseIn", ClampMin = "0.0", ClampMax = "1.0"))
	float Phase = 0.0f;

	/** Keep stepping while the game is paused (real time instead of game time). Only used when Time isn't connected. */
	UPROPERTY(EditAnywhere, Category = "Stepped Time")
	bool bIgnorePause = false;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif
};
