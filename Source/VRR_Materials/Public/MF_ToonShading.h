#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialExpression.h"
#include "MF_ToonShading.generated.h"

/**
 * Toon shading with a fake (art-directed) light. Scene lights are ignored.
 * Plug Emissive into an Unlit material's Emissive Color.
 *
 *   NdotL     = dot(N, LightDirection)
 *   LightMask = NdotL above Threshold, split into Bands with a Softness-wide edge, times Shadow
 *   Diffuse   = BaseColor * lerp(ShadowColor, LightColor, LightMask)
 *   Specular  = smoothstep around NdotH = 1 - SpecularSize, lit side only
 *   Rim       = smoothstep around (1 - NdotV) = 1 - RimWidth
 *   Emissive  = Diffuse + Specular * LightColor * SpecularIntensity + Rim * RimColor * RimIntensity
 *
 * For one light shared by every toon material, use the MF_ToonLit material function,
 * which feeds this node from MPC_ToonLighting.
 */
UCLASS(collapsecategories, hidecategories=Object)
class VRR_MATERIALS_API UMaterialExpressionVrrToonShading : public UMaterialExpression
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrToonShading(const FObjectInitializer& ObjectInitializer);

	// Inputs

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'BaseColor' if not specified"))
	FExpressionInput BaseColorIn;

	/** Surface normal. Defaults to the mesh normal. */
	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Normal map or world normal (see 'Normal Is Tangent Space'). Defaults to the mesh normal"))
	FExpressionInput Normal;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "World-space direction towards the light. Defaults to 'LightDirection' if not specified"))
	FExpressionInput LightDirectionIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'LightColor' if not specified"))
	FExpressionInput LightColorIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'ShadowColor' if not specified"))
	FExpressionInput ShadowColorIn;

	/** 0-1 cast shadow (e.g. from Vrr Toon Shadow). 0 = in shadow. Defaults to fully lit. */
	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "0-1 cast shadow, 0 = in shadow. Defaults to fully lit"))
	FExpressionInput Shadow;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Threshold' if not specified"))
	FExpressionInput ThresholdIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Softness' if not specified"))
	FExpressionInput SoftnessIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Bands' if not specified"))
	FExpressionInput BandsIn;

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

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#endif
};
