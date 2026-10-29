#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DeveloperSettings.h"
#include "Subsystems/WorldSubsystem.h"
#include "Vrr_ToonBoil.generated.h"

class UMaterialParameterCollection;
class UPrimitiveComponent;
class USkinnedMeshComponent;

/**
 * "Boil time" for the toon noise (outline line boil, shading band wobble): a clock that only runs while things move,
 * so a still frame stays still like a held drawing, and the lines start boiling again as soon as anything moves.
 *
 *   Camera:  UVrr_ToonBoilSubsystem advances MPC_ToonLighting.BoilTime while the player camera moves or turns.
 *            Every toon material gets this, with nothing to set up.
 *   Objects: a Vrr Toon Boil component on an actor runs that actor's own clock while the actor moves, its skeleton
 *            animates (idles and interactions count, see Animation), or the camera moves, and writes it to Custom
 *            Primitive Data on all the actor's primitives:
 *              [CPD::BoilTime]   = the actor's boil time
 *              [CPD::BoilActive] = 1 (0 when there's no component, so materials fall back to the camera clock)
 *
 * Materials read it as lerp(MPC BoilTime, CPD BoilTime, CPD BoilActive) and feed that into Vrr Stepped Time's Time pin.
 * The slots live in VrrToonLighting.h; keep them in sync with the "ToonBoil..." CPD parameters in M_Toon_Outline,
 * M_Toon_Base and M_Toon_BlobShadow.
 */

/** Project Settings > Plugins > VRR Toon Boil. */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "VRR Toon Boil"))
class VRR_MATERIALS_API UVrr_ToonBoilSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UVrr_ToonBoilSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Collection that receives BoilTime (camera clock). Must have a BoilTime scalar. */
	UPROPERTY(config, EditAnywhere, Category = "Toon Boil")
	TSoftObjectPtr<UMaterialParameterCollection> LightingCollection;

	/** Camera or actor counts as moving above this speed (cm/s). Keeps tiny jitter (spring arms, idle sway) from boiling. */
	UPROPERTY(config, EditAnywhere, Category = "Toon Boil", meta = (ClampMin = "0.0", UIMax = "200.0"))
	float MinMoveSpeed = 10.0f;

	/** ...or above this turn rate (degrees/s). */
	UPROPERTY(config, EditAnywhere, Category = "Toon Boil", meta = (ClampMin = "0.0", UIMax = "90.0"))
	float MinTurnSpeed = 3.0f;

	/** In-place animation (Bone Motion on the component) counts as moving when the fastest bone moves faster than this
	 *  (cm/s, in world units, relative to the mesh). Low enough for idles, high enough to ignore a held pose's noise. */
	UPROPERTY(config, EditAnywhere, Category = "Toon Boil", meta = (ClampMin = "0.0", UIMax = "50.0"))
	float MinAnimationSpeed = 2.0f;

	/** Keep boiling this long after motion stops (s), so a short stop doesn't freeze mid-step. 0 = freeze at once. */
	UPROPERTY(config, EditAnywhere, Category = "Toon Boil", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float HoldTime = 0.0f;

	/** Editor viewports and material previews have no player camera. On: the clock always runs there, so you can
	 *  tune the noise while editing. Off: it stays frozen outside play. */
	UPROPERTY(config, EditAnywhere, Category = "Toon Boil")
	bool bAlwaysAnimateInEditor = true;
};

/** Tracks one transform and says whether it moved faster than the settings' thresholds since the last update. */
struct FVrrToonMotionTracker
{
	/** True while moving (or within Hold Time of the last movement). The first update never counts as motion. */
	bool Update(const FVector& Location, const FQuat& Rotation, float DeltaSeconds, const UVrr_ToonBoilSettings& Settings);

	void Reset() { bHasLast = false; HoldLeft = 0.0f; }

private:
	FVector LastLocation = FVector::ZeroVector;
	FQuat LastRotation = FQuat::Identity;
	float HoldLeft = 0.0f;
	bool bHasLast = false;
};

/** Whether a Vrr Toon Boil component also boils while its actor animates in place. */
UENUM(BlueprintType)
enum class EVrrToonBoilAnimation : uint8
{
	/** Only actor movement (and the camera) count. */
	Ignore,
	/** Skeletal meshes count as moving while their bones move (idles, interactions), measured relative to the mesh. */
	BoneMotion,
	/** Always boil, e.g. vertex-animated (VAT) meshes whose motion the CPU can't see. */
	Always
};

/** Runs the camera boil clock and writes it to MPC_ToonLighting.BoilTime. Created automatically in every world. */
UCLASS()
class VRR_MATERIALS_API UVrr_ToonBoilSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return true; }

	/** True while the player camera moves or turns (always true outside play with Always Animate In Editor). */
	UFUNCTION(BlueprintPure, Category = "Toon Boil")
	bool IsCameraMoving() const { return bCameraMoving; }

	/** Seconds the camera has spent moving. */
	UFUNCTION(BlueprintPure, Category = "Toon Boil")
	float GetCameraBoilTime() const { return CameraBoilTime; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> Collection;

	FVrrToonMotionTracker CameraMotion;
	float CameraBoilTime = 0.0f;
	bool bCameraMoving = false;
};

/**
 * Gives an actor its own boil clock: its outline and band noise animate while the actor moves, animates in place
 * (see Animation) or the camera moves, and hold still otherwise. Add it to anything that moves on its own (player, enemies, moving props). Actors without it boil
 * only while the camera moves. Only runs in game; in the editor materials use the camera clock.
 */
UCLASS(ClassGroup = (Rendering), meta = (BlueprintSpawnableComponent, DisplayName = "Vrr Toon Boil"))
class VRR_MATERIALS_API UVrr_ToonBoilComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVrr_ToonBoilComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Also boil while the camera moves (the drawing is redrawn when the shot changes). Off = only the actor's own motion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toon Boil")
	bool bBoilWithCamera = true;

	/** Also boil while the actor animates in place (idles, interactions). Bone Motion watches its skeletal meshes;
	 *  Always is for vertex-animated meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toon Boil")
	EVrrToonBoilAnimation Animation = EVrrToonBoilAnimation::BoneMotion;

	/** Re-collect the actor's primitive components, e.g. after adding or swapping meshes at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Toon Boil")
	void RefreshPrimitives();

	/** Seconds this actor has spent boiling. */
	UFUNCTION(BlueprintPure, Category = "Toon Boil")
	float GetBoilTime() const { return BoilTime; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Last pose of one skeletal mesh, to measure how fast its bones move. */
	struct FPoseTracker
	{
		TWeakObjectPtr<USkinnedMeshComponent> Mesh;
		TArray<FVector> LastPositions;	// component space
		float SinceChange = 0.0f;		// seconds since the pose last changed (animation can skip frames)
		bool bMoving = false;
	};

	/** True while any tracked skeleton's fastest bone moves faster than Min Animation Speed. */
	bool UpdatePoses(float DeltaTime, const UVrr_ToonBoilSettings& Settings);

	/** Writes the boil time and active flag to every primitive (Active = 0 hands them back to the camera clock). */
	void WriteToPrimitives(float Active);

	TArray<TWeakObjectPtr<UPrimitiveComponent>> Primitives;
	TArray<FPoseTracker> Poses;
	FVrrToonMotionTracker Motion;
	float AnimationHoldLeft = 0.0f;
	float BoilTime = 0.0f;
	float WrittenTime = -1.0f;
};
