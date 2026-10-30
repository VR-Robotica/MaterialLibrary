// Copyright VR Robotica. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
#include "MF_CelShadow.generated.h"

/**
 * Cel layer drop shadow: meshes tagged by a Vrr Cel Layer component look like animation cels lying on top of the
 * background, with a flat shadow offset behind them. Use in a Post Process material at "Scene Color Before DOF"
 * (before TSR, so the shadow edges get anti-aliased). Needs Custom Depth-Stencil Pass = "Enabled with Stencil".
 *
 *   Source = the pixel Offset away (up-left for a down-right shadow)
 *   Mask   = Source is a cel
 *          * this pixel isn't in front of that cel (more than Ground Tolerance)
 *          * if this pixel is a cel too, it lies at least Layer Gap behind (another character, not the same one)
 *   Color  = lerp(Scene Color, Shadow Color, Mask * Opacity)
 */
UCLASS(collapsecategories, hidecategories=Object)
class MATERIALLIBRARY_API UMaterialExpressionVrrCelShadow : public UMaterialExpressionVrrBase
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrCelShadow(const FObjectInitializer& ObjectInitializer);

	// Inputs

	UPROPERTY(meta = (VrrType = "V2", RequiredInput = "false", ToolTip = "Viewport UV to work at. Defaults to this pixel"))
	FExpressionInput UV;

	UPROPERTY(meta = (VrrType = "V3", RequiredInput = "false", ToolTip = "Scene color to shadow. Defaults to PostProcessInput0"))
	FExpressionInput SceneColorIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Float2, in 1080p pixels. Defaults to 'Offset' if not specified"))
	FExpressionInput OffsetIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Opacity' if not specified"))
	FExpressionInput OpacityIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'ShadowColor' if not specified"))
	FExpressionInput ShadowColorIn;

	// Look

	/** Shadow offset from the cel in pixels at 1080p (scales with resolution). +X right, +Y down. */
	UPROPERTY(EditAnywhere, Category = "Cel Shadow", meta = (OverridingInputProperty = "OffsetIn"))
	FVector2D Offset = FVector2D(6.0, 6.0);

	/** How much the shadow covers what's behind it (0 = none, 1 = solid Shadow Color). */
	UPROPERTY(EditAnywhere, Category = "Cel Shadow", meta = (OverridingInputProperty = "OpacityIn", ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Cel Shadow", meta = (OverridingInputProperty = "ShadowColorIn"))
	FLinearColor ShadowColor = FLinearColor::Black;

	// Receivers

	/**
	 * Surfaces up to this far (cm) in front of the cel still receive its shadow, e.g. the ground right at its feet.
	 * Anything closer to the camera than that (foreground props) stays clear.
	 */
	UPROPERTY(EditAnywhere, Category = "Receivers", meta = (ClampMin = "0.0", UIMax = "200.0"))
	float GroundTolerance = 50.0f;

	/**
	 * Another cel receives the shadow only when it's at least this far (cm) behind the casting pixel, so a character
	 * never shadows itself (an arm over its own body) but does shadow a character standing behind it.
	 */
	UPROPERTY(EditAnywhere, Category = "Receivers", meta = (ClampMin = "0.0", UIMax = "500.0"))
	float LayerGap = 100.0f;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif
};
