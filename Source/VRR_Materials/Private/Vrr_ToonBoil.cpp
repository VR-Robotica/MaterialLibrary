/*
 * Toon boil clock: time for the toon noise that only runs while the camera or an object moves.
 *
 * Strategy
 *  Hand-drawn line boil comes from redrawing every frame of animation; a held frame doesn't boil. Shaders can't tell
 *  whether anything moved (they have no memory between frames), so motion is detected here on the game thread and
 *  turned into a clock that only advances while moving. Materials use that clock instead of game time, through
 *  Vrr Stepped Time, so the noise steps at its usual frame rate while things move and freezes when they stop.
 *   - Camera clock (subsystem): global, written to MPC_ToonLighting.BoilTime. Covers every toon material.
 *   - Actor clock (component):  runs while the actor moves, animates in place, or the camera moves, written per primitive as Custom Primitive
 *                               Data, plus an "active" flag so materials know to use it instead of the camera clock.
 *  One clock per actor (not camera + actor added together) keeps the speed the same when both move.
 *
 * Steps (each frame)
 *  1. Motion: compare the transform with last frame; moving = speed above Min Move Speed or turn rate above
 *     Min Turn Speed, held for Hold Time after it stops.
 *     In-place animation (component, Bone Motion): each skeletal mesh's bones are compared with their last pose in
 *     component space (so walking doesn't count twice), scaled to world units, and the fastest bone has to beat
 *     Min Animation Speed. Animation can skip frames (budgets, URO), so an unchanged pose keeps the last answer for
 *     a moment instead of counting as stopped.
 *  2. Clock: while moving, add the frame's delta time.
 *  3. Publish: subsystem -> MPC BoilTime; component -> CPD [BoilTimeIndex] = time, [BoilActiveIndex] = 1.
 */

#include "Vrr_ToonBoil.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

namespace VrrToonBoil
{
	/** An unchanged pose counts as "animation skipped a frame" for this long before it counts as a held pose. */
	constexpr float PoseSkipTolerance = 0.25f;

	/** Scalar in MPC_ToonLighting that receives the camera clock. Must match the collection and the toon materials. */
	static const FName BoilTimeParam(TEXT("BoilTime"));
}

// SETTINGS

UVrr_ToonBoilSettings::UVrr_ToonBoilSettings()
{
	LightingCollection = TSoftObjectPtr<UMaterialParameterCollection>(
		FSoftObjectPath(TEXT("/VRR_Materials/Masters/Toon/MPC_ToonLighting.MPC_ToonLighting")));
}

// MOTION

bool FVrrToonMotionTracker::Update(const FVector& Location, const FQuat& Rotation, float DeltaSeconds, const UVrr_ToonBoilSettings& Settings)
{
	// 1. Speed and turn rate since last frame (the first update has nothing to compare with)
	bool bMovedNow = false;
	if (bHasLast && DeltaSeconds > UE_SMALL_NUMBER)
	{
		const float Speed = FVector::Dist(Location, LastLocation) / DeltaSeconds;
		const float TurnRate = FMath::RadiansToDegrees(Rotation.AngularDistance(LastRotation)) / DeltaSeconds;
		bMovedNow = Speed > Settings.MinMoveSpeed || TurnRate > Settings.MinTurnSpeed;
	}
	LastLocation = Location;
	LastRotation = Rotation;
	bHasLast = true;

	// 2. Keep counting as moving for Hold Time after the motion stops
	if (bMovedNow)
	{
		HoldLeft = Settings.HoldTime;
		return true;
	}
	if (HoldLeft > 0.0f)
	{
		HoldLeft -= DeltaSeconds;
		return true;
	}
	return false;
}

// SUBSYSTEM

bool UVrr_ToonBoilSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Editor previews (material editor, asset viewers) too, so toon materials animate there as well
	return Super::DoesSupportWorldType(WorldType) || WorldType == EWorldType::EditorPreview;
}

void UVrr_ToonBoilSubsystem::Initialize(FSubsystemCollectionBase& InCollection)
{
	Super::Initialize(InCollection);
	Collection = GetDefault<UVrr_ToonBoilSettings>()->LightingCollection.LoadSynchronous();
}

TStatId UVrr_ToonBoilSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVrr_ToonBoilSubsystem, STATGROUP_Tickables);
}

void UVrr_ToonBoilSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const UVrr_ToonBoilSettings& Settings = *GetDefault<UVrr_ToonBoilSettings>();

	// 1. Motion: the first player's camera in game; outside play there's no player camera
	if (World->IsGameWorld())
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager.Get() : nullptr;
		if (Camera)
		{
			bCameraMoving = CameraMotion.Update(Camera->GetCameraLocation(), Camera->GetCameraRotation().Quaternion(), DeltaTime, Settings);
		}
		else
		{
			CameraMotion.Reset();
			bCameraMoving = false;
		}
	}
	else
	{
		bCameraMoving = Settings.bAlwaysAnimateInEditor;
	}

	// 2. Clock
	if (!bCameraMoving)
	{
		return;
	}
	CameraBoilTime += DeltaTime;

	// 3. Publish to the collection (per-world instance, so PIE and the editor world keep separate clocks)
	if (Collection)
	{
		if (UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Collection))
		{
			Instance->SetScalarParameterValue(VrrToonBoil::BoilTimeParam, CameraBoilTime);
		}
	}
}

// COMPONENT

UVrr_ToonBoilComponent::UVrr_ToonBoilComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;	// after the actor has moved this frame
	bTickInEditor = false;
}

void UVrr_ToonBoilComponent::BeginPlay()
{
	Super::BeginPlay();
	Motion.Reset();
	RefreshPrimitives();
}

void UVrr_ToonBoilComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Hand the primitives back to the camera clock
	WriteToPrimitives(0.0f);
	Super::EndPlay(EndPlayReason);
}

void UVrr_ToonBoilComponent::RefreshPrimitives()
{
	Primitives.Reset();
	Poses.Reset();
	if (const AActor* Owner = GetOwner())
	{
		TInlineComponentArray<UPrimitiveComponent*> Found(Owner);
		for (UPrimitiveComponent* Primitive : Found)
		{
			Primitives.Add(Primitive);

			// Skeletons to watch for in-place animation. Followers (Leader Pose) just copy their leader, so skip them.
			USkinnedMeshComponent* Skinned = Cast<USkinnedMeshComponent>(Primitive);
			if (Skinned && !Skinned->LeaderPoseComponent.IsValid())
			{
				FPoseTracker& Pose = Poses.AddDefaulted_GetRef();
				Pose.Mesh = Skinned;
			}
		}
	}
	WrittenTime = -1.0f;	// force a write so new primitives get the flag
}

void UVrr_ToonBoilComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// 1. Motion: the actor's own, its in-place animation, or the camera's
	const UVrr_ToonBoilSettings& Settings = *GetDefault<UVrr_ToonBoilSettings>();
	const bool bOwnerMoving = Motion.Update(Owner->GetActorLocation(), Owner->GetActorQuat(), DeltaTime, Settings);

	bool bAnimating = Animation == EVrrToonBoilAnimation::Always;
	if (Animation == EVrrToonBoilAnimation::BoneMotion)
	{
		// Same Hold Time as movement, so short pauses in an idle don't freeze it mid-step
		if (UpdatePoses(DeltaTime, Settings))
		{
			AnimationHoldLeft = Settings.HoldTime;
			bAnimating = true;
		}
		else if (AnimationHoldLeft > 0.0f)
		{
			AnimationHoldLeft -= DeltaTime;
			bAnimating = true;
		}
	}

	bool bCameraMoving = false;
	if (bBoilWithCamera)
	{
		if (const UVrr_ToonBoilSubsystem* Subsystem = GetWorld()->GetSubsystem<UVrr_ToonBoilSubsystem>())
		{
			bCameraMoving = Subsystem->IsCameraMoving();
		}
	}

	// 2. Clock
	if (bOwnerMoving || bAnimating || bCameraMoving)
	{
		BoilTime += DeltaTime;
	}

	// 3. Publish (only when it changed: each write re-uploads the primitive's data)
	if (BoilTime != WrittenTime)
	{
		WriteToPrimitives(1.0f);
	}
}

bool UVrr_ToonBoilComponent::UpdatePoses(float DeltaTime, const UVrr_ToonBoilSettings& Settings)
{
	bool bAnyMoving = false;
	for (FPoseTracker& Pose : Poses)
	{
		const USkinnedMeshComponent* Mesh = Pose.Mesh.Get();
		if (!Mesh)
		{
			continue;
		}
		const TArray<FTransform>& Bones = Mesh->GetComponentSpaceTransforms();

		// 1. New mesh, or the bone count changed (mesh swapped): just remember this pose
		if (Pose.LastPositions.Num() != Bones.Num())
		{
			Pose.LastPositions.SetNumUninitialized(Bones.Num());
			for (int32 i = 0; i < Bones.Num(); ++i)
			{
				Pose.LastPositions[i] = Bones[i].GetTranslation();
			}
			Pose.SinceChange = 0.0f;
			Pose.bMoving = false;
			continue;
		}

		// 2. Fastest bone since the pose last changed, in world units (the component's scale applies, e.g. a rig with
		//    x100 bones under a 0.01 scaled mesh), but relative to the mesh so the actor's own movement doesn't count
		const FVector Scale = Mesh->GetComponentTransform().GetScale3D().GetAbs();
		double MaxDistSq = 0.0;
		for (int32 i = 0; i < Bones.Num(); ++i)
		{
			const FVector Position = Bones[i].GetTranslation();
			MaxDistSq = FMath::Max(MaxDistSq, ((Position - Pose.LastPositions[i]) * Scale).SizeSquared());
			Pose.LastPositions[i] = Position;
		}
		Pose.SinceChange += DeltaTime;

		// 3. Unchanged pose: animation may have skipped this frame, so keep the last answer for a moment
		if (MaxDistSq <= 0.0)
		{
			Pose.bMoving = Pose.bMoving && Pose.SinceChange < VrrToonBoil::PoseSkipTolerance;
		}
		else
		{
			// Speed over the whole time since the last change, so skipped frames don't inflate it
			Pose.bMoving = FMath::Sqrt(MaxDistSq) / FMath::Max(Pose.SinceChange, UE_SMALL_NUMBER) > Settings.MinAnimationSpeed;
			Pose.SinceChange = 0.0f;
		}
		bAnyMoving |= Pose.bMoving;
	}
	return bAnyMoving;
}

void UVrr_ToonBoilComponent::WriteToPrimitives(float Active)
{
	for (const TWeakObjectPtr<UPrimitiveComponent>& Weak : Primitives)
	{
		if (UPrimitiveComponent* Primitive = Weak.Get())
		{
			Primitive->SetCustomPrimitiveDataFloat(VrrToonBoil::BoilTimeIndex, BoilTime);
			Primitive->SetCustomPrimitiveDataFloat(VrrToonBoil::BoilActiveIndex, Active);
		}
	}
	WrittenTime = BoilTime;
}
