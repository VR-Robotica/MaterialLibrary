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
 *   - centers an orthographic depth capture on the player, pushed ahead along the camera's facing by View Offset
 *     (or on itself in the editor),
 *   - writes the capture's view into the collection (ShadowOrigin/Right/Up/Forward) for Vrr Toon Shadow.
 * Without a caster in the level, toon materials are simply unshadowed.
 */
UCLASS(meta = (DisplayName = "Vrr Toon Shadow Caster"))
class MATERIALLIBRARY_API AVrr_ToonShadowCaster : public AActor
{
	GENERATED_BODY()

public:
	AVrr_ToonShadowCaster();

	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

	/**
	 * Leaves Actor out of the shadow map, so it stops casting toon shadows (it still receives them).
	 * Include Attached also covers actors attached to it (gear spawned as separate actors). Safe to call twice.
	 */
	UFUNCTION(BlueprintCallable, Category = "Toon Shadow", meta = (AdvancedDisplay = "bIncludeAttached"))
	void AddIgnoredActor(AActor* Actor, bool bIncludeAttached = true);

	/** Puts Actor (and, with Include Attached, the actors attached to it) back into the shadow map. */
	UFUNCTION(BlueprintCallable, Category = "Toon Shadow", meta = (AdvancedDisplay = "bIncludeAttached"))
	void RemoveIgnoredActor(AActor* Actor, bool bIncludeAttached = true);

	/** Actors currently left out of the shadow map. */
	UFUNCTION(BlueprintPure, Category = "Toon Shadow")
	TArray<AActor*> GetIgnoredActors() const;

	/**
	 * Add Ignored Actor on every Toon Shadow Caster in the world (does nothing when there is none).
	 * Lets an actor opt out from its own Begin Play without finding the caster first.
	 */
	UFUNCTION(BlueprintCallable, Category = "Toon Shadow", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "bIncludeAttached"))
	static void IgnoreInToonShadows(const UObject* WorldContextObject, AActor* Actor, bool bIncludeAttached = true);

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

	/**
	 * How far back towards the light the capture sits, in cm. Must clear anything that should cast a shadow.
	 * Keep it as short as that allows: the stored depth grows with this distance and loses precision as it does
	 * (at 5000 the error outgrows the depth bias and lit surfaces get diamond-shaped self-shadowing acne).
	 */
	UPROPERTY(EditAnywhere, Category = "Toon Shadow", meta = (ClampMin = "100.0", UIMax = "3000.0"))
	float CaptureDistance = 1500.0f;

	/** In game, center the shadowed area on the player's view target. In the editor it always uses this actor's location. */
	UPROPERTY(EditAnywhere, Category = "Toon Shadow")
	bool bFollowPlayer = true;

	/**
	 * With Follow Player, shifts the shadowed area this far (cm) ahead of the player along the camera's horizontal
	 * facing, so less of the map is spent behind the camera. Scaled down as the camera looks down (none when looking
	 * straight down). Keep it under half of Shadow Area so the player stays inside the map. 0 = centered on the player.
	 */
	UPROPERTY(EditAnywhere, Category = "Toon Shadow", meta = (ClampMin = "0.0", UIMax = "5000.0", EditCondition = "bFollowPlayer"))
	float ViewOffset = 750.0f;

	/** Seconds the View Offset takes to catch up when the camera turns, so the map doesn't jump. 0 = instant. */
	UPROPERTY(EditAnywhere, Category = "Toon Shadow", meta = (ClampMin = "0.0", UIMax = "2.0", EditCondition = "bFollowPlayer"))
	float ViewOffsetSmoothing = 0.25f;

	/**
	 * Actors left out of the shadow map, so they never cast toon shadows (e.g. one-sided planes or helpers above the
	 * level). At runtime, change it with Add / Remove Ignored Actor: edits to the array itself don't reach the capture.
	 */
	UPROPERTY(EditAnywhere, Category = "Toon Shadow")
	TArray<TObjectPtr<AActor>> IgnoredActors;

	UPROPERTY(VisibleAnywhere, Category = "Toon Shadow")
	TObjectPtr<USceneCaptureComponent2D> Capture;

private:
	/** Moves the capture to follow the center and light, and writes its view into the collection. */
	void UpdateCapture(float DeltaSeconds = 0.0f);

	/** Center of the shadowed area; advances the smoothed View Offset by DeltaSeconds (0 = snap to it). */
	FVector GetShadowCenter(float DeltaSeconds);
	FVector GetLightDirection() const;

	/** View Offset currently applied, eased towards the camera's facing (world space, horizontal). */
	FVector SmoothedViewOffset = FVector::ZeroVector;

	/** Sets ShadowOrigin.w to 0 so toon materials stop sampling the map. */
	void DisableShadows();

	/** Actor plus, optionally, everything attached to it (recursively). */
	static TArray<AActor*> GatherActors(AActor* Actor, bool bIncludeAttached);

	/** Hands the valid entries to the capture; in play, also drops destroyed ones from the list. */
	void ApplyIgnoredActors();
};
