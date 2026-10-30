// Copyright VR Robotica. All Rights Reserved.

/*
 * Toon shadow caster: renders the depth map that Vrr Toon Shadow reads, from the toon fake light.
 *
 * Strategy
 *  Toon materials are Unlit, so engine shadow maps never reach them. Instead this actor runs a small depth-only
 *  scene capture that looks along the fake light (read from MPC_ToonLighting), and publishes where that capture is
 *  (origin, axes, width, resolution) back into the same collection. Vrr Toon Shadow then projects each pixel into
 *  the map with those values and does the depth test itself.
 *  The capture and the published values must always describe the same view, so the capture is triggered manually
 *  right after the values are written, never on its own schedule.
 *
 * Per update
 *  1. Point the capture along the light (Forward = -LightDirection) and build its Right / Up axes.
 *  2. Center on the player pushed ahead along the camera's facing by View Offset (smoothed), or on this actor in
 *     the editor; snapped to whole texels so shadows don't shimmer as it moves.
 *  3. Back the capture off toward the light by Capture Distance.
 *  4. Write ShadowOrigin / Right / Up / Forward into the collection.
 *  5. Render the capture (deferred in game, immediate in the editor).
 *  When the actor goes away it sets the map width to 0, which Vrr Toon Shadow treats as "fully lit".
 */

#include "Vrr_ToonShadowCaster.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "VrrToonLighting.h"

/** Sets up a cheap, depth-only, orthographic capture that only renders when UpdateCapture asks it to. */
AVrr_ToonShadowCaster::AVrr_ToonShadowCaster()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;	// after the camera has moved this frame

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(RootComponent);
	Capture->SetUsingAbsoluteLocation(true);
	Capture->SetUsingAbsoluteRotation(true);

	// Orthographic depth-only view from the light. Fixed planes so depth is measured from the capture position.
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->OrthoWidth = ShadowArea;
	Capture->bAutoCalculateOrthoPlanes = false;
	Capture->bUpdateOrthoPlanes = false;
	Capture->CaptureSource = SCS_SceneDepth;
	// Captures are triggered from UpdateCapture so they always match the values written to the collection
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;

	// Only depth matters, so skip the expensive parts of the frame
	FEngineShowFlags& Flags = Capture->ShowFlags;
	Flags.SetAtmosphere(false);
	Flags.SetFog(false);
	Flags.SetVolumetricFog(false);
	Flags.SetPostProcessing(false);
	Flags.SetBloom(false);
	Flags.SetMotionBlur(false);
	Flags.SetDynamicShadows(false);
	Flags.SetTranslucency(false);
	Flags.SetParticles(false);
	Flags.SetDecals(false);
	Flags.SetSkyLighting(false);
	Flags.SetAmbientOcclusion(false);
	Flags.SetScreenSpaceReflections(false);
	Flags.SetReflectionEnvironment(false);
	Flags.SetGlobalIllumination(false);
	Flags.SetLumenGlobalIllumination(false);
	Flags.SetLumenReflections(false);
}

/** Runs when placed, moved or edited: pushes the editable settings onto the capture and refreshes it. */
void AVrr_ToonShadowCaster::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Capture->TextureTarget = ShadowTarget;
	ApplyIgnoredActors();

	// Editor viewports that aren't realtime don't tick actors, so also update when placed, moved or edited
	UpdateCapture();
}

void AVrr_ToonShadowCaster::AddIgnoredActor(AActor* Actor, bool bIncludeAttached)
{
	for (AActor* Each : GatherActors(Actor, bIncludeAttached))
	{
		IgnoredActors.AddUnique(Each);
	}
	ApplyIgnoredActors();
}

void AVrr_ToonShadowCaster::RemoveIgnoredActor(AActor* Actor, bool bIncludeAttached)
{
	for (AActor* Each : GatherActors(Actor, bIncludeAttached))
	{
		IgnoredActors.Remove(Each);
	}
	ApplyIgnoredActors();
}

TArray<AActor*> AVrr_ToonShadowCaster::GetIgnoredActors() const
{
	TArray<AActor*> Result;
	for (AActor* Each : IgnoredActors)
	{
		if (IsValid(Each))
		{
			Result.Add(Each);
		}
	}
	return Result;
}

void AVrr_ToonShadowCaster::IgnoreInToonShadows(const UObject* WorldContextObject, AActor* Actor, bool bIncludeAttached)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || !Actor)
	{
		return;
	}

	for (TActorIterator<AVrr_ToonShadowCaster> It(World); It; ++It)
	{
		It->AddIgnoredActor(Actor, bIncludeAttached);
	}
}

TArray<AActor*> AVrr_ToonShadowCaster::GatherActors(AActor* Actor, bool bIncludeAttached)
{
	TArray<AActor*> Actors;
	if (IsValid(Actor))
	{
		Actors.Add(Actor);
		if (bIncludeAttached)
		{
			TArray<AActor*> Attached;
			Actor->GetAttachedActors(Attached, /*bResetArray=*/ true, /*bRecursivelyIncludeAttachedActors=*/ true);
			Actors.Append(Attached);
		}
	}
	return Actors;
}

void AVrr_ToonShadowCaster::ApplyIgnoredActors()
{
	auto IsGone = [](const TObjectPtr<AActor>& Each) { return !IsValid(Each); };

	// In play, a respawned player leaves its destroyed pawn behind in the list: drop it. Not in the editor, where an
	// empty entry is the slot the Details panel's "+" just added (editing reruns OnConstruction, which lands here)
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		IgnoredActors.RemoveAll(IsGone);
	}

	TArray<TObjectPtr<AActor>> Hidden = IgnoredActors;
	Hidden.RemoveAll(IsGone);
	Capture->HiddenActors = MoveTemp(Hidden);
}

void AVrr_ToonShadowCaster::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateCapture(DeltaSeconds);
}

void AVrr_ToonShadowCaster::UpdateCapture(float DeltaSeconds)
{
	// Nothing to do until the collection and render target are set
	UWorld* World = GetWorld();
	if (!World || !LightingCollection || !ShadowTarget)
	{
		return;
	}
	Capture->TextureTarget = ShadowTarget;

	// 1. Capture looks along the light, away from it. Right / Up are the map's U / V axes.
	const FVector Forward = -GetLightDirection();
	const FRotator Rotation = FRotationMatrix::MakeFromX(Forward).Rotator();
	const FRotationMatrix Axes(Rotation);
	const FVector Right = Axes.GetUnitAxis(EAxis::Y);
	const FVector Up = Axes.GetUnitAxis(EAxis::Z);

	// 2. Snap the center to whole texels across the map so shadow edges don't shimmer while moving
	//    (only Right / Up are snapped; depth along Forward doesn't affect which texel a point lands in)
	const float Resolution = FMath::Max(ShadowTarget->SizeX, 1);
	const double TexelSize = ShadowArea / Resolution;
	const FVector Center = GetShadowCenter(DeltaSeconds);
	const double SnappedRight = FMath::GridSnap(FVector::DotProduct(Center, Right), TexelSize);
	const double SnappedUp = FMath::GridSnap(FVector::DotProduct(Center, Up), TexelSize);
	const double Depth = FVector::DotProduct(Center, Forward);
	const FVector SnappedCenter = Right * SnappedRight + Up * SnappedUp + Forward * Depth;

	// 3. Back off toward the light so everything that can cast a shadow is in front of the capture
	const FVector CapturePosition = SnappedCenter - Forward * CaptureDistance;
	Capture->OrthoWidth = ShadowArea;
	Capture->SetWorldLocationAndRotation(CapturePosition, Rotation);

	// 4. Tell toon materials where the map is (layout documented on Vrr Toon Shadow), in this world's collection
	if (UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(LightingCollection))
	{
		Instance->SetVectorParameterValue(VrrToonLighting::ShadowOrigin, FLinearColor(CapturePosition.X, CapturePosition.Y, CapturePosition.Z, ShadowArea));
		Instance->SetVectorParameterValue(VrrToonLighting::ShadowRight, FLinearColor(Right.X, Right.Y, Right.Z, Resolution));
		Instance->SetVectorParameterValue(VrrToonLighting::ShadowUp, FLinearColor(Up.X, Up.Y, Up.Z, 0.0f));
		Instance->SetVectorParameterValue(VrrToonLighting::ShadowForward, FLinearColor(Forward.X, Forward.Y, Forward.Z, 0.0f));
	}

	// 5. In game, queue the capture with the main frame render (cheapest). In the editor the main view may not
	// render (non-realtime viewports), so render it now to keep the map in sync with the values above.
	if (World->IsGameWorld())
	{
		Capture->CaptureSceneDeferred();
	}
	else
	{
		Capture->CaptureScene();
	}
}

/**
 * In game (with Follow Player), the player's view target pushed ahead by View Offset along the camera's horizontal
 * facing; otherwise this actor's location.
 */
FVector AVrr_ToonShadowCaster::GetShadowCenter(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (bFollowPlayer && World && World->IsGameWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const AActor* ViewTarget = PC->GetViewTarget())
			{
				// 1. Target offset: camera forward flattened onto the ground. Its length is cos(pitch), so the offset
				//    shrinks as the camera looks down, where the visible ground sits around the player anyway.
				FVector TargetOffset = FVector::ZeroVector;
				if (ViewOffset > 0.0f && PC->PlayerCameraManager)
				{
					const FVector CameraForward = PC->PlayerCameraManager->GetCameraRotation().Vector();
					TargetOffset = FVector(CameraForward.X, CameraForward.Y, 0.0f) * ViewOffset;
				}

				// 2. Ease towards it (frame-rate independent), so turning the camera doesn't make the map jump
				const float Alpha = (DeltaSeconds <= 0.0f || ViewOffsetSmoothing <= 0.0f)
					? 1.0f : 1.0f - FMath::Exp(-DeltaSeconds / ViewOffsetSmoothing);
				SmoothedViewOffset = FMath::Lerp(SmoothedViewOffset, TargetOffset, Alpha);

				return ViewTarget->GetActorLocation() + SmoothedViewOffset;
			}
		}
	}
	SmoothedViewOffset = FVector::ZeroVector;
	return GetActorLocation();
}

/** LightDirection from the collection's instance in this world (normalized), or straight up if it can't be read. */
FVector AVrr_ToonShadowCaster::GetLightDirection() const
{
	FLinearColor Value(0.0f, 0.0f, 1.0f);
	if (const UWorld* World = GetWorld())
	{
		if (const UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(LightingCollection))
		{
			Instance->GetVectorParameterValue(VrrToonLighting::LightDirection, Value);
		}
	}
	const FVector Direction(Value.R, Value.G, Value.B);
	return Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
}

/** Width 0 in ShadowOrigin.w tells Vrr Toon Shadow there is no map, so toon materials render fully lit. */
void AVrr_ToonShadowCaster::DisableShadows()
{
	if (LightingCollection && GetWorld())
	{
		UKismetMaterialLibrary::SetVectorParameterValue(this, LightingCollection, VrrToonLighting::ShadowOrigin, FLinearColor::Transparent);
	}
}

void AVrr_ToonShadowCaster::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DisableShadows();
	Super::EndPlay(EndPlayReason);
}

void AVrr_ToonShadowCaster::Destroyed()
{
	DisableShadows();
	Super::Destroyed();
}
