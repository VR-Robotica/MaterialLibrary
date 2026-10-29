#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
#include "MF_TapeGrade.generated.h"

/**
 * Worn 1990s video tape color grade for a Post Process material (after tonemapping), applied to Color
 * (e.g. Vrr Tape Signal):
 *
 *  - Levels: milky, lifted blacks and soft, dim highlights (camcorder knee), so the picture looks low contrast.
 *  - Faded color: less saturation overall, and color drains out of shadows (tape noise) and highlights.
 *  - Bleeding reds: reds stay hot and oversaturated while everything else fades.
 *  - Tints: teal-green shadows, warm yellow highlights.
 *  - Hue drift: a slow hue wobble that rolls down the screen, like a tape that never plays back the same color twice.
 *
 * Wear blends every effect from a clean signal (0) to the values set here (1). Above 1 pushes them further.
 */
UCLASS(collapsecategories, hidecategories=Object)
class VRR_MATERIALS_API UMaterialExpressionVrrTapeGrade : public UMaterialExpressionVrrBase
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrTapeGrade(const FObjectInitializer& ObjectInitializer);

	/** The picture (e.g. Vrr Tape Signal). */
	UPROPERTY(meta = (VrrType = "V3", RequiredInput = "true"))
	FExpressionInput Color;

	UPROPERTY(meta = (VrrType = "V2", RequiredInput = "false", ToolTip = "Viewport UV, usually Vrr Tape Warp's UV, so the hue drift follows the warped picture. Defaults to this pixel"))
	FExpressionInput UV;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Wear' if not specified"))
	FExpressionInput WearIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Black Level' if not specified"))
	FExpressionInput BlackLevelIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Saturation' if not specified"))
	FExpressionInput SaturationIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Hue Drift' if not specified"))
	FExpressionInput HueDriftIn;

	/** How worn the tape is: 0 = clean signal (no grade), 1 = the values below, above 1 = even more worn. */
	UPROPERTY(EditAnywhere, Category = "Tape Grade", meta = (OverridingInputProperty = "WearIn", ClampMin = "0.0", UIMax = "1.5"))
	float Wear = 1.0f;

	/** Brightness of black: tape blacks are lifted and milky. 0 = true black. */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Levels", meta = (OverridingInputProperty = "BlackLevelIn", ClampMin = "0.0", UIMax = "0.25"))
	float BlackLevel = 0.07f;

	/** Brightness of a full white. Below 1 gives dim, grayish whites. */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Levels", meta = (ClampMin = "0.5", UIMax = "1.0"))
	float WhiteLevel = 0.97f;

	/**
	 * Brightness where highlights start to compress (camcorder knee). Above it, brightness rolls off softly:
	 * a full white lands halfway between Knee and 1, and brighter values (e.g. dropouts) approach 1. 1 = no knee.
	 */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Levels", meta = (ClampMin = "0.3", ClampMax = "1.0"))
	float Knee = 0.75f;

	/** Below 1 brightens the midtones (washed out), above 1 darkens them. */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Levels", meta = (ClampMin = "0.25", UIMax = "2.0"))
	float Gamma = 0.92f;

	/** Overall color strength. Below 1 fades the color. */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Color", meta = (OverridingInputProperty = "SaturationIn", ClampMin = "0.0", UIMax = "2.0"))
	float Saturation = 0.72f;

	/** How much color drains out of the darkest tones. 0 = none, 1 = gray shadows. */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Color", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ShadowFade = 0.55f;

	/** How much color drains out of the brightest tones. 0 = none, 1 = white highlights. */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Color", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HighlightFade = 0.45f;

	/** Extra saturation on reds and oranges: on tape they stay hot and bleed while other colors fade. */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Color", meta = (ClampMin = "0.0", UIMax = "1.5"))
	float RedBoost = 0.6f;

	/** Hue rotation of the whole picture, in degrees. Negative leans toward green, positive toward magenta. */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Color", meta = (UIMin = "-30.0", UIMax = "30.0"))
	float HueShift = -3.0f;

	/** Color added to the shadows (only its hue counts, not its brightness). */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Tint", meta = (HideAlphaChannel))
	FLinearColor ShadowTint = FLinearColor(0.15f, 0.42f, 0.40f);

	/** Color added to the highlights (only its hue counts, not its brightness). */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Tint", meta = (HideAlphaChannel))
	FLinearColor HighlightTint = FLinearColor(1.0f, 0.86f, 0.55f);

	/** How strong both tints are. 0 = no tint. */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Tint", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float TintStrength = 0.25f;

	/** Largest hue wobble, in degrees. 0 = steady color (cheaper). */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Hue Drift", meta = (OverridingInputProperty = "HueDriftIn", ClampMin = "0.0", UIMax = "30.0"))
	float HueDrift = 6.0f;

	/** How fast the wobble changes, in changes per second. */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Hue Drift", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float HueDriftSpeed = 0.4f;

	/** How many hue bands fit over the screen height. */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Hue Drift", meta = (ClampMin = "0.0", UIMax = "8.0"))
	float HueDriftBands = 1.5f;

	/** Keep the drift moving while the game is paused (real time). */
	UPROPERTY(EditAnywhere, Category = "Tape Grade|Hue Drift")
	bool bIgnorePause = true;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif
};
