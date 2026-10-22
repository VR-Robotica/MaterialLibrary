#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vrr_ToonShadowCaster.generated.h"

class UMaterialParameterCollection;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * Renders a depth map from the toon fake light so toon materials get cast and self shadows.
 * Place one in the level. Each frame it:
 *   - reads LightDirection from the Lighting Collection (MPC_ToonLighting),
 *   - centers an orthographic depth capture on the player (or on itself in the editor),
 *   - writes the capture's view into the collection (ShadowOrigin/Right/Up/Forward) for Vrr Toon Shadow.
 * Without a caster in the level, toon materials are simply unshadowed.
 */
UCLASS(meta = (DisplayName = "Vrr Toon Shadow Caster"))
class VRR_MATERIALS_API AVrr_ToonShadowCaster : public AActor
{
	GENERATED_BODY()

public:
	AVrr_ToonShadowCaster();

	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;

	/** MPC_ToonLighting: LightDirection is read from it, the Shadow* values are written to it. */
	UPROPERTY(EditAnywhere, Category = "Toon Shadow")
	TObjectPtr<UMaterialParameterCollection> LightingCollection;

	/** R32f render target the depth map is drawn into. Must be the same one set on Vrr Toon Shadow in MF_ToonLit. */
	UPROPERTY(EditAnywhere, Category = "Toon Shadow")
	TObjectPtr<UTextureRenderTarget2D> ShadowTarget;

	/** Width of the shadowed area around the player, in cm. Smaller = sharper shadows. */
	UPROPERTY(EditAnywhere, Category = "Toon Shadow", meta = (ClampMin = "100.0", UIMax = "20000.0"))
	float ShadowArea = 3000.0f;

	/** How far back towards the light the capture sits, in cm. Must clear anything that should cast a shadow. */
	UPROPERTY(EditAnywhere, Category = "Toon Shadow", meta = (ClampMin = "100.0"))
	float CaptureDistance = 5000.0f;

	/** In game, center the shadowed area on the player's view target. In the editor it always uses this actor's location. */
	UPROPERTY(EditAnywhere, Category = "Toon Shadow")
	bool bFollowPlayer = true;

	/** Actors left out of the shadow map, so they never cast toon shadows (e.g. one-sided planes or helpers above the level). */
	UPROPERTY(EditAnywhere, Category = "Toon Shadow")
	TArray<TObjectPtr<AActor>> IgnoredActors;

	UPROPERTY(VisibleAnywhere, Category = "Toon Shadow")
	TObjectPtr<USceneCaptureComponent2D> Capture;

private:
	/** Moves the capture to follow the center and light, and writes its view into the collection. */
	void UpdateCapture();

	FVector GetShadowCenter() const;
	FVector GetLightDirection() const;

	/** Sets ShadowOrigin.w to 0 so toon materials stop sampling the map. */
	void DisableShadows();
};
