#include "Vrr_ToonShadowCaster.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

namespace ToonShadowParams
{
	static const FName LightDirection(TEXT("LightDirection"));
	static const FName ShadowOrigin(TEXT("ShadowOrigin"));
	static const FName ShadowRight(TEXT("ShadowRight"));
	static const FName ShadowUp(TEXT("ShadowUp"));
	static const FName ShadowForward(TEXT("ShadowForward"));
}

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

void AVrr_ToonShadowCaster::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Capture->TextureTarget = ShadowTarget;
	Capture->HiddenActors = IgnoredActors;

	// Editor viewports that aren't realtime don't tick actors, so also update when placed, moved or edited
	UpdateCapture();
}

void AVrr_ToonShadowCaster::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateCapture();
}

void AVrr_ToonShadowCaster::UpdateCapture()
{
	UWorld* World = GetWorld();
	if (!World || !LightingCollection || !ShadowTarget)
	{
		return;
	}
	Capture->TextureTarget = ShadowTarget;

	// Capture looks along the light, away from it
	const FVector Forward = -GetLightDirection();
	const FRotator Rotation = FRotationMatrix::MakeFromX(Forward).Rotator();
	const FRotationMatrix Axes(Rotation);
	const FVector Right = Axes.GetUnitAxis(EAxis::Y);
	const FVector Up = Axes.GetUnitAxis(EAxis::Z);

	// Snap the center to whole texels across the map so shadow edges don't shimmer while moving
	const float Resolution = FMath::Max(ShadowTarget->SizeX, 1);
	const double TexelSize = ShadowArea / Resolution;
	const FVector Center = GetShadowCenter();
	const double SnappedRight = FMath::GridSnap(FVector::DotProduct(Center, Right), TexelSize);
	const double SnappedUp = FMath::GridSnap(FVector::DotProduct(Center, Up), TexelSize);
	const double Depth = FVector::DotProduct(Center, Forward);
	const FVector SnappedCenter = Right * SnappedRight + Up * SnappedUp + Forward * Depth;

	const FVector CapturePosition = SnappedCenter - Forward * CaptureDistance;
	Capture->OrthoWidth = ShadowArea;
	Capture->SetWorldLocationAndRotation(CapturePosition, Rotation);

	// Tell toon materials where the map is
	UKismetMaterialLibrary::SetVectorParameterValue(this, LightingCollection, ToonShadowParams::ShadowOrigin,
		FLinearColor(CapturePosition.X, CapturePosition.Y, CapturePosition.Z, ShadowArea));
	UKismetMaterialLibrary::SetVectorParameterValue(this, LightingCollection, ToonShadowParams::ShadowRight,
		FLinearColor(Right.X, Right.Y, Right.Z, Resolution));
	UKismetMaterialLibrary::SetVectorParameterValue(this, LightingCollection, ToonShadowParams::ShadowUp,
		FLinearColor(Up.X, Up.Y, Up.Z, 0.0f));
	UKismetMaterialLibrary::SetVectorParameterValue(this, LightingCollection, ToonShadowParams::ShadowForward,
		FLinearColor(Forward.X, Forward.Y, Forward.Z, 0.0f));

	// In game, queue the capture with the main frame render (cheapest). In the editor the main view may not
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

FVector AVrr_ToonShadowCaster::GetShadowCenter() const
{
	UWorld* World = GetWorld();
	if (bFollowPlayer && World && World->IsGameWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const AActor* ViewTarget = PC->GetViewTarget())
			{
				return ViewTarget->GetActorLocation();
			}
		}
	}
	return GetActorLocation();
}

FVector AVrr_ToonShadowCaster::GetLightDirection() const
{
	FLinearColor Value(0.0f, 0.0f, 1.0f);
	if (const UWorld* World = GetWorld())
	{
		if (const UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(LightingCollection))
		{
			Instance->GetVectorParameterValue(ToonShadowParams::LightDirection, Value);
		}
	}
	const FVector Direction(Value.R, Value.G, Value.B);
	return Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
}

void AVrr_ToonShadowCaster::DisableShadows()
{
	if (LightingCollection && GetWorld())
	{
		UKismetMaterialLibrary::SetVectorParameterValue(this, LightingCollection, ToonShadowParams::ShadowOrigin, FLinearColor::Transparent);
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
