// Copyright VR Robotica. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
#include "MF_ToonShading.generated.h"

/**
 * Toon shading with a fake (art-directed) light. Scene lights are ignored.
 * Plug Emissive into an Unlit material's Emissive Color.
 *
 *   NdotL     = dot(N, LightDirection) + BandOffset
 *   LightMask = NdotL above Threshold, split into Bands with a Softness-wide edge, times Shadow
 *   Bounce    = flat band where dot(N, BounceDirection) > BounceThreshold, shadow side only (optional)
 *   EdgeLine  = thin light line on the shadow side of each band edge, EdgeLineWidth px wide (optional)
 *   Metallic  = flat band on the shadow side, deeper than MetallicThreshold of it, times Metallic (optional)
 *   Lighting  = lerp(ShadowColor, LightColor, max(LightMask, Metallic * 0.5) + EdgeLine * (1 - EdgeLineColorBlend) / Bands)
 *   Lighting *= lerp(1, CastShadowColor, (1 - Shadow) * CastShadowColorBlend * LightMask without Shadow)
 *               (blob-style cast shadows, optional: that share of Shadow is then left out of LightMask above)
 *   Diffuse   = BaseColor * (Lighting + BounceColor * Bounce * BounceIntensity)
 *   Specular  = smoothstep around NdotH = 1 - SpecularSize, lit side only
 *   Rim       = smoothstep around (1 - NdotV) = 1 - RimWidth
 *   Emissive  = Diffuse + Specular * LightColor * SpecularIntensity + Rim * RimColor * RimIntensity
 *   Emissive  = lerp(Emissive, EdgeLineColor, EdgeLine * EdgeLineColorBlend)   (colored / white edge line, optional)
 *
 * For one light shared by every toon material, use the MF_ToonLit material function,
 * which feeds this node from MPC_ToonLighting.
 */
UCLASS(collapsecategories, hidecategories=Object)
class MATERIALLIBRARY_API UMaterialExpressionVrrToonShading : public UMaterialExpressionVrrBase
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrToonShading(const FObjectInitializer& ObjectInitializer);

	// Inputs

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'BaseColor' if not specified"))
	FExpressionInput BaseColorIn;

	/** Surface normal. Defaults to the mesh normal. */
	UPROPERTY(meta = (VrrType = "V3", RequiredInput = "false", ToolTip = "Normal map or world normal (see 'Normal Is Tangent Space'). Defaults to the mesh normal"))
	FExpressionInput Normal;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "World-space direction towards the light. Defaults to 'LightDirection' if not specified"))
	FExpressionInput LightDirectionIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'LightColor' if not specified"))
	FExpressionInput LightColorIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'ShadowColor' if not specified"))
	FExpressionInput ShadowColorIn;

	/** 0-1 cast shadow (e.g. from Vrr Toon Shadow). 0 = in shadow. Defaults to fully lit. */
	UPROPERTY(meta = (VrrType = "S", RequiredInput = "false", ToolTip = "0-1 cast shadow, 0 = in shadow. Defaults to fully lit"))
	FExpressionInput Shadow;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'CastShadowColor' if not specified"))
	FExpressionInput CastShadowColorIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'CastShadowColorBlend' if not specified"))
	FExpressionInput CastShadowColorBlendIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Threshold' if not specified"))
	FExpressionInput ThresholdIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Softness' if not specified"))
	FExpressionInput SoftnessIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Bands' if not specified"))
	FExpressionInput BandsIn;

	/** Per-pixel shift of the band edges, in NdotL units (e.g. small animated noise for a hand-drawn wobble). Defaults to 0. */
	UPROPERTY(meta = (VrrType = "S", RequiredInput = "false", ToolTip = "Per-pixel shift of the band edges in NdotL units, e.g. noise * 0.05 for a hand-drawn wobble. Moves the light and bounce band edges only. Defaults to 0"))
	FExpressionInput BandOffset;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'EdgeLineWidth' if not specified"))
	FExpressionInput EdgeLineWidthIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'EdgeLineIntensity' if not specified"))
	FExpressionInput EdgeLineIntensityIn;

	/** 0-1 multiplier along the edge line, e.g. noise, so it breaks up like a hand-painted cel. Defaults to 1. */
	UPROPERTY(meta = (VrrType = "S", RequiredInput = "false", ToolTip = "0-1 multiplier along the edge line (e.g. noise) so it breaks up and varies like a hand-painted cel. Defaults to 1"))
	FExpressionInput EdgeLineMask;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'EdgeLineColor' if not specified"))
	FExpressionInput EdgeLineColorIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'EdgeLineColorBlend' if not specified"))
	FExpressionInput EdgeLineColorBlendIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Metallic' if not specified"))
	FExpressionInput MetallicIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'MetallicThreshold' if not specified"))
	FExpressionInput MetallicThresholdIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'SpecularSize' if not specified"))
	FExpressionInput SpecularSizeIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'SpecularIntensity' if not specified"))
	FExpressionInput SpecularIntensityIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'RimColor' if not specified"))
	FExpressionInput RimColorIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'RimWidth' if not specified"))
	FExpressionInput RimWidthIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'RimIntensity' if not specified"))
	FExpressionInput RimIntensityIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "World-space direction towards the bounce source. Defaults to 'BounceDirection' if not specified"))
	FExpressionInput BounceDirectionIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'BounceColor' if not specified"))
	FExpressionInput BounceColorIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'BounceIntensity' if not specified"))
	FExpressionInput BounceIntensityIn;

	// Surface

	UPROPERTY(EditAnywhere, Category = "Surface", meta = (OverridingInputProperty = "BaseColorIn"))
	FLinearColor BaseColor = FLinearColor(0.8f, 0.8f, 0.8f);

	/** On: Normal is a tangent-space normal map (most textures). Off: Normal is already in world space. */
	UPROPERTY(EditAnywhere, Category = "Surface")
	bool bNormalIsTangentSpace = true;

	// Fake light

	/** World-space direction pointing towards the light. Does not need to be normalized. */
	UPROPERTY(EditAnywhere, Category = "Light", meta = (OverridingInputProperty = "LightDirectionIn"))
	FVector LightDirection = FVector(0.5, 0.3, 0.8);

	UPROPERTY(EditAnywhere, Category = "Light", meta = (OverridingInputProperty = "LightColorIn"))
	FLinearColor LightColor = FLinearColor(1.0f, 0.97f, 0.9f);

	/** Tint applied to BaseColor in shadow. Acts as the ambient light. */
	UPROPERTY(EditAnywhere, Category = "Light", meta = (OverridingInputProperty = "ShadowColorIn"))
	FLinearColor ShadowColor = FLinearColor(0.32f, 0.3f, 0.42f);

	/**
	 * Color a cast shadow (Shadow input) multiplies the lit surface by, the same way M_Toon_BlobShadow shades the floor.
	 * Only used with Cast Shadow Color Blend above 0. Darker = stronger shadow.
	 */
	UPROPERTY(EditAnywhere, Category = "Light", meta = (OverridingInputProperty = "CastShadowColorIn"))
	FLinearColor CastShadowColor = FLinearColor(0.28f, 0.45f, 0.49f);

	/**
	 * How cast shadows are colored. 0 = like the shading's own shadow side (Shadow Color), 1 = the lit color
	 * multiplied by Cast Shadow Color (matches the blob shadow). Surfaces already on the shadow side keep their tone.
	 */
	UPROPERTY(EditAnywhere, Category = "Light", meta = (OverridingInputProperty = "CastShadowColorBlendIn", ClampMin = "0.0", ClampMax = "1.0"))
	float CastShadowColorBlend = 0.0f;

	// Shading bands

	/** NdotL where shadow starts. 0 = terminator at 90 degrees, negative = more lit, positive = more shadow. */
	UPROPERTY(EditAnywhere, Category = "Bands", meta = (OverridingInputProperty = "ThresholdIn", UIMin = "-1.0", UIMax = "0.9"))
	float Threshold = 0.0f;

	/** Width of the soft edge between bands, in NdotL units. Near 0 = hard cel edge. */
	UPROPERTY(EditAnywhere, Category = "Bands", meta = (OverridingInputProperty = "SoftnessIn", ClampMin = "0.0", UIMax = "0.5"))
	float Softness = 0.02f;

	/** Number of lit tones above the shadow. 1 = classic two-tone cel shading. */
	UPROPERTY(EditAnywhere, Category = "Bands", meta = (OverridingInputProperty = "BandsIn", ClampMin = "1", UIMax = "6"))
	float Bands = 1.0f;

	// Edge line

	/** Width of the light line along the shadow side of each band edge, in pixels at 1080p (scales with resolution). */
	UPROPERTY(EditAnywhere, Category = "Edge Line", meta = (OverridingInputProperty = "EdgeLineWidthIn", ClampMin = "0.0", UIMax = "8.0"))
	float EdgeLineWidth = 3.0f;

	/** How far the line lifts the shadow toward the next band's tone. 0 turns it off (and compiles it out unless the pin is wired). */
	UPROPERTY(EditAnywhere, Category = "Edge Line", meta = (OverridingInputProperty = "EdgeLineIntensityIn", ClampMin = "0.0", UIMax = "1.0"))
	float EdgeLineIntensity = 0.0f;

	/** Color the line is painted with as Edge Line Color Blend goes to 1. White (or brighter, HDR) makes the line
	 *  bright enough for post-process glow such as Vrr Halation to pick it up. */
	UPROPERTY(EditAnywhere, Category = "Edge Line", meta = (OverridingInputProperty = "EdgeLineColorIn", HideAlphaChannel))
	FLinearColor EdgeLineColor = FLinearColor::White;

	/** 0 = the line lifts the shadow toward the next band's tone; 1 = the line is painted in Edge Line Color.
	 *  0 compiles the color path out unless the pin is wired. */
	UPROPERTY(EditAnywhere, Category = "Edge Line", meta = (OverridingInputProperty = "EdgeLineColorBlendIn", ClampMin = "0.0", ClampMax = "1.0"))
	float EdgeLineColorBlend = 0.0f;

	// Metallic

	/**
	 * Fake metallic reflection: one extra flat band on the shadow side, halfway between the lit and shadow tones
	 * (at 1). 0 turns it off (and compiles it out unless the pin is wired).
	 */
	UPROPERTY(EditAnywhere, Category = "Metallic", meta = (OverridingInputProperty = "MetallicIn", ClampMin = "0.0", ClampMax = "1.0"))
	float Metallic = 0.0f;

	/**
	 * Where the metallic band starts, as a share of the shadow side measured from the terminator: 0 = the band covers
	 * the whole shadow side, 0.5 = its deeper half (a dark strip stays along the terminator), 1 = nothing.
	 */
	UPROPERTY(EditAnywhere, Category = "Metallic", meta = (OverridingInputProperty = "MetallicThresholdIn", ClampMin = "0.0", ClampMax = "1.0"))
	float MetallicThreshold = 0.3f;

	// Specular

	/** Size of the toon highlight. Larger = bigger spot. */
	UPROPERTY(EditAnywhere, Category = "Specular", meta = (OverridingInputProperty = "SpecularSizeIn", ClampMin = "0.0", UIMax = "0.3"))
	float SpecularSize = 0.03f;

	/** 0 turns the highlight off. */
	UPROPERTY(EditAnywhere, Category = "Specular", meta = (OverridingInputProperty = "SpecularIntensityIn", ClampMin = "0.0"))
	float SpecularIntensity = 0.5f;

	// Rim

	UPROPERTY(EditAnywhere, Category = "Rim", meta = (OverridingInputProperty = "RimColorIn"))
	FLinearColor RimColor = FLinearColor::White;

	/** How far the rim reaches in from the silhouette (0-1). */
	UPROPERTY(EditAnywhere, Category = "Rim", meta = (OverridingInputProperty = "RimWidthIn", ClampMin = "0.0", ClampMax = "1.0"))
	float RimWidth = 0.3f;

	/** 0 turns the rim off. */
	UPROPERTY(EditAnywhere, Category = "Rim", meta = (OverridingInputProperty = "RimIntensityIn", ClampMin = "0.0"))
	float RimIntensity = 0.5f;

	/** 0 = rim all around the silhouette, 1 = rim only on the lit side. */
	UPROPERTY(EditAnywhere, Category = "Rim", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RimLightAlign = 0.5f;

	// Bounce light

	/** World-space direction pointing towards the bounce source. Default: from below, like light off the ground. */
	UPROPERTY(EditAnywhere, Category = "Bounce", meta = (OverridingInputProperty = "BounceDirectionIn"))
	FVector BounceDirection = FVector(0.0, 0.0, -1.0);

	/** Bounce tint, multiplied with Base Color. Usually the ground's color. */
	UPROPERTY(EditAnywhere, Category = "Bounce", meta = (OverridingInputProperty = "BounceColorIn"))
	FLinearColor BounceColor = FLinearColor(0.55f, 0.42f, 0.32f);

	/** How bright the bounce is. 0 turns it off (and compiles it out unless the pin is wired). */
	UPROPERTY(EditAnywhere, Category = "Bounce", meta = (OverridingInputProperty = "BounceIntensityIn", ClampMin = "0.0", UIMax = "2.0"))
	float BounceIntensity = 0.0f;

	/** How far the bounce band reaches: dot(N, BounceDirection) above this gets it. Higher = a thinner band. */
	UPROPERTY(EditAnywhere, Category = "Bounce", meta = (UIMin = "-0.5", UIMax = "0.9"))
	float BounceThreshold = 0.2f;

	/** On: bounce only shows on the shadow side (classic). Off: it also adds to lit areas. */
	UPROPERTY(EditAnywhere, Category = "Bounce")
	bool bBounceInShadowOnly = true;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif
};
