#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
#include "MF_HitFlash.generated.h"

/**
 * Hit flash that fades out after a hit.
 * Plug a scalar parameter holding the hit time (e.g. "TimeToHit", set from code with World->TimeSeconds) into HitTime.
 *
 *   Alpha    = saturate(1 - (Time - HitTime) / Duration) ^ 2
 *   Emissive = Color * Intensity * Alpha
 */
UCLASS(collapsecategories, hidecategories=Object)
class VRR_MATERIALS_API UMaterialExpressionVrrHitFlash : public UMaterialExpressionVrrBase
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrHitFlash(const FObjectInitializer& ObjectInitializer);

	/** World time of the last hit (usually a scalar parameter). */
	UPROPERTY(meta = (VrrType = "S", RequiredInput = "true"))
	FExpressionInput HitTime;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Color' if not specified"))
	FExpressionInput ColorIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Duration' if not specified"))
	FExpressionInput DurationIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Intensity' if not specified"))
	FExpressionInput IntensityIn;

	/** Flash color. */
	UPROPERTY(EditAnywhere, Category = "Hit Flash", meta = (OverridingInputProperty = "ColorIn"))
	FLinearColor Color = FLinearColor::White;

	/** Seconds for the flash to fade out. */
	UPROPERTY(EditAnywhere, Category = "Hit Flash", meta = (OverridingInputProperty = "DurationIn", ClampMin = "0.01"))
	float Duration = 0.25f;

	/** Emissive multiplier. */
	UPROPERTY(EditAnywhere, Category = "Hit Flash", meta = (OverridingInputProperty = "IntensityIn"))
	float Intensity = 4.0f;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif
};
