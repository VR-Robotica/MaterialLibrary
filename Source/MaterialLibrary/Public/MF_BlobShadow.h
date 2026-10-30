// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
#include "MF_BlobShadow.generated.h"

/**
 * Cel-shaded blob shadow, projected like a decal from a box mesh (see UVrr_BlobShadowComponent).
 * Use in a Translucent (alpha), Unlit, Two Sided material with Disable Depth Test on, drawn on a 100 cm cube
 * (M_Toon_BlobShadow):
 *   Target   = SceneColor * lerp(1, ShadowColor, Strength)
 *   Coverage = Mask * (ground not in toon cast shadow) * (not under the cel drop shadow)
 *   Color    = Target * Coverage, Transmittance = 1 - Coverage
 * Every blob blends toward the same target (the scene before translucency, shadowed once), so overlapping blobs
 * don't multiply into a darker patch the way Modulate did, and it works on lit and Unlit (toon) floors alike.
 *
 *   Ground    = the scene point behind the pixel (from scene depth), in box space (-1..1 on every axis)
 *   r         = length(Ground.xy) - Noise(edge direction, Time * NoiseSpeed) * NoiseAmount
 *   Mask      = hard ellipse r < 1 (anti-aliased over EdgeSoftness px), fading out toward the box top / bottom,
 *               only on ground facing up the box, never on Custom Depth objects (characters), back faces only
 *
 * World Position / World Normal give the ground point under the pixel, so the material can test it against other
 * shadows (Vrr Toon Shadow's World Position In) and not darken ground that's already shaded.
 */
UCLASS(collapsecategories, hidecategories=Object)
class MATERIALLIBRARY_API UMaterialExpressionVrrBlobShadow : public UMaterialExpressionVrrBase
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrBlobShadow(const FObjectInitializer& ObjectInitializer);

	// Inputs

	/** Clock for the edge wobble, e.g. Vrr Stepped Time (held steps like the outline boil). Defaults to game time. */
	UPROPERTY(meta = (VrrType = "S", RequiredInput = "false", ToolTip = "Clock for the edge wobble, e.g. Vrr Stepped Time so it steps like the outline. Defaults to game time"))
	FExpressionInput TimeIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'NoiseAmount' if not specified"))
	FExpressionInput NoiseAmountIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'NoiseScale' if not specified"))
	FExpressionInput NoiseScaleIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'NoiseSpeed' if not specified"))
	FExpressionInput NoiseSpeedIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'EdgeSoftness' if not specified"))
	FExpressionInput EdgeSoftnessIn;

	// Shape

	/** How far the edge wobbles in and out, as a fraction of the radius. 0 = a clean ellipse. */
	UPROPERTY(EditAnywhere, Category = "Blob Shadow", meta = (OverridingInputProperty = "NoiseAmountIn", ClampMin = "0.0", UIMax = "0.2"))
	float NoiseAmount = 0.04f;

	/** Wobbles around the edge. Higher = more, smaller bumps. */
	UPROPERTY(EditAnywhere, Category = "Blob Shadow", meta = (OverridingInputProperty = "NoiseScaleIn", ClampMin = "0.0", UIMax = "8.0"))
	float NoiseScale = 2.5f;

	/** How fast the wobble pattern moves per unit of Time. 0 = static. */
	UPROPERTY(EditAnywhere, Category = "Blob Shadow", meta = (OverridingInputProperty = "NoiseSpeedIn", ClampMin = "0.0", UIMax = "8.0"))
	float NoiseSpeed = 2.0f;

	/** Width of the anti-aliased edge in screen pixels. ~1 = a crisp cel edge. */
	UPROPERTY(EditAnywhere, Category = "Blob Shadow", meta = (OverridingInputProperty = "EdgeSoftnessIn", ClampMin = "0.1", UIMax = "8.0"))
	float EdgeSoftness = 1.0f;

	// Receivers

	/** Where (0-1 of the box half height, from the ground plane) the shadow starts fading toward the box top / bottom. */
	UPROPERTY(EditAnywhere, Category = "Receivers", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	float HeightFadeStart = 0.5f;

	/** Only surfaces facing at least this much along the box up axis receive it (0 = everything, 1 = flat only). */
	UPROPERTY(EditAnywhere, Category = "Receivers", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	float MinUpFacing = 0.5f;

	/** Skip pixels drawn by objects that render Custom Depth (the blob shadow component turns it on for its owner),
	 *  so the character standing in its own shadow isn't darkened. */
	UPROPERTY(EditAnywhere, Category = "Receivers")
	bool bExcludeCustomDepth = true;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif
};
