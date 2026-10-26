#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
#include "MF_Dissolve.generated.h"

/**
 * Noise-driven dissolve with a glowing burn edge.
 * Plug a 0-1 noise value (e.g. a texture's R channel) into Noise, and drive Amount from 0 (solid) to 1 (gone).
 *
 *   Threshold   = Amount * (1 + EdgeWidth) - EdgeWidth
 *   OpacityMask = step(Threshold, Noise)
 *   Edge        = (1 - saturate((Noise - Threshold) / EdgeWidth)) * OpacityMask
 *   Emissive    = EdgeColor * EdgeIntensity * Edge
 */
UCLASS(collapsecategories, hidecategories=Object)
class VRR_MATERIALS_API UMaterialExpressionVrrDissolve : public UMaterialExpressionVrrBase
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrDissolve(const FObjectInitializer& ObjectInitializer);

	/** 0-1 noise value that decides which pixels dissolve first. */
	UPROPERTY(meta = (RequiredInput = "true"))
	FExpressionInput Noise;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Amount' if not specified"))
	FExpressionInput AmountIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'EdgeWidth' if not specified"))
	FExpressionInput EdgeWidthIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'EdgeColor' if not specified"))
	FExpressionInput EdgeColorIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'EdgeIntensity' if not specified"))
	FExpressionInput EdgeIntensityIn;

	/** 0 = fully visible, 1 = fully dissolved. */
	UPROPERTY(EditAnywhere, Category = "Dissolve", meta = (OverridingInputProperty = "AmountIn", ClampMin = "0.0", ClampMax = "1.0"))
	float Amount = 0.0f;

	/** Width of the glowing edge, in noise units (0-1). */
	UPROPERTY(EditAnywhere, Category = "Dissolve", meta = (OverridingInputProperty = "EdgeWidthIn", ClampMin = "0.001"))
	float EdgeWidth = 0.05f;

	/** Color of the burn edge. */
	UPROPERTY(EditAnywhere, Category = "Dissolve", meta = (OverridingInputProperty = "EdgeColorIn"))
	FLinearColor EdgeColor = FLinearColor(1.0f, 0.35f, 0.05f);

	/** Emissive multiplier for the edge. */
	UPROPERTY(EditAnywhere, Category = "Dissolve", meta = (OverridingInputProperty = "EdgeIntensityIn"))
	float EdgeIntensity = 10.0f;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif
};
