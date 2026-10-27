/*
 * Vrr Blob Shadow component: keeps a cel-shaded blob shadow under its owner.
 *
 * Strategy
 *  The look lives in the material (Vrr Blob Shadow node, M_Toon_BlobShadow); this component only places the box it
 *  draws on. The box is a 100 cm cube in world space (absolute transform), so the owner's rotation and scale never
 *  stretch it:
 *   - Size: the owner's visible meshes, measured in actor space (so turning doesn't change it), times WidthScale.
 *     Skeletal bounds move with the animation, so the size eases toward the new value instead of pulsing.
 *   - Place: a trace straight down from the footprint's center (ignoring the owner) finds the ground; the box is
 *     centered on the hit, yawed with the owner and optionally tilted to the slope. The material receives the
 *     shadow around the box's middle plane, so the box's half height is how much ground variation it covers.
 *   - Air: the higher the owner is above the ground, the smaller and fainter the blob (Custom Primitive Data 0).
 *  It also runs in the editor (OnRegister + editor ticking), so the shadow shows while placing actors.
 *
 * Steps (every tick)
 *  1. Footprint of the owner's meshes, smoothed.
 *  2. Trace down for the ground; hide when there's none.
 *  3. Height above ground -> size and opacity.
 *  4. Box transform and opacity.
 */

#include "Vrr_BlobShadow.h"

#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace VrrBlobShadow
{
	/** Custom Primitive Data index the material reads the opacity from. */
	constexpr int32 OpacityIndex = 0;

	/** The engine cube is 100 cm. */
	constexpr float CubeSize = 100.0f;

	/** Material used when none is assigned. */
	constexpr const TCHAR* DefaultMaterialPath = TEXT("/VRR_Materials/Masters/Toon/M_Toon_BlobShadow.M_Toon_BlobShadow");
}

UVrr_BlobShadowComponent::UVrr_BlobShadowComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;	// after movement and ragdoll, so it doesn't lag a frame
	bTickInEditor = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		SetStaticMesh(Cube.Object);
	}

	// A pure visual: no collision, no shadows, no decals, nothing for navigation or overlaps
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	CastShadow = false;
	bReceivesDecals = false;
	bSelectable = false;

	// World-space box: the owner's rotation and scale must not stretch it
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);

	SetDefaultCustomPrimitiveDataFloat(VrrBlobShadow::OpacityIndex, 1.0f);
}

void UVrr_BlobShadowComponent::OnRegister()
{
	Super::OnRegister();

	// Default look (loaded here rather than in the constructor so a missing asset never breaks class loading)
	if (OverrideMaterials.Num() == 0 || !OverrideMaterials[0])
	{
		if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, VrrBlobShadow::DefaultMaterialPath))
		{
			SetMaterial(0, Material);
		}
	}

	UpdateBlob(0.0f);
}

void UVrr_BlobShadowComponent::BeginPlay()
{
	Super::BeginPlay();

	// Let the material leave the owner out of its own shadow
	if (bExcludeOwner)
	{
		if (AActor* Owner = GetOwner())
		{
			TInlineComponentArray<UMeshComponent*> Meshes(Owner);
			for (UMeshComponent* Mesh : Meshes)
			{
				if (Mesh != this)
				{
					Mesh->SetRenderCustomDepth(true);
				}
			}
		}
	}

	SmoothedSize = FVector2D(-1.0, -1.0);
	UpdateBlob(0.0f);
}

void UVrr_BlobShadowComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateBlob(DeltaTime);
}

bool UVrr_BlobShadowComponent::GetOwnerFootprint(FVector& OutTop, FVector& OutBottom, FVector2D& OutSize) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	// Combined bounds of the visible meshes in actor space (rotation-independent)
	const FTransform ActorTransform = Owner->GetActorTransform();
	FBox Box(ForceInit);
	TInlineComponentArray<UMeshComponent*> Meshes(Owner);
	for (const UMeshComponent* Mesh : Meshes)
	{
		if (Mesh == this || !Mesh->IsVisible() || !Mesh->IsRegistered())
		{
			continue;
		}
		const FTransform MeshInActor = Mesh->GetComponentTransform().GetRelativeTransform(ActorTransform);
		Box += Mesh->CalcBounds(MeshInActor).GetBox();
	}
	if (!Box.IsValid)
	{
		return false;
	}

	OutTop = ActorTransform.TransformPosition(FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z));
	OutBottom = ActorTransform.TransformPosition(FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Min.Z));
	const FVector Size = Box.GetSize() * ActorTransform.GetScale3D().GetAbs();
	OutSize = FVector2D(Size.X, Size.Y);
	return true;
}

void UVrr_BlobShadowComponent::UpdateBlob(float DeltaSeconds)
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	// 1. Footprint (size eased toward the current bounds so animation doesn't make it pulse)
	FVector TraceStart;
	FVector Bottom;
	FVector2D Size;
	if (!GetOwnerFootprint(TraceStart, Bottom, Size))
	{
		TraceStart = Bottom = Owner->GetActorLocation();
		Size = FVector2D(100.0, 100.0);
	}
	if (FixedWidth > 0.0f)
	{
		Size = FVector2D(FixedWidth, FixedWidth);
	}
	else
	{
		if (bRound)
		{
			const double Average = 0.5 * (Size.X + Size.Y);
			Size = FVector2D(Average, Average);
		}
		Size *= WidthScale;
	}
	if (SmoothedSize.X < 0.0 || SizeSmoothing <= 0.0f || DeltaSeconds <= 0.0f)
	{
		SmoothedSize = Size;
	}
	else
	{
		SmoothedSize = FMath::Vector2DInterpTo(SmoothedSize, Size, DeltaSeconds, SizeSmoothing);
	}

	// 2. Ground below the owner (the owner itself is ignored)
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VrrBlobShadow), false, Owner);
	const FVector TraceEnd(TraceStart.X, TraceStart.Y, Bottom.Z - MaxGroundDistance);
	FHitResult Hit;
	const bool bGround = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params);
	if (IsVisible() != bGround)
	{
		SetVisibility(bGround);
	}
	if (!bGround)
	{
		return;
	}

	// 3. Height of the owner's lowest point above the ground: shrink and fade while in the air
	const double Height = FMath::Max(0.0, Bottom.Z - Hit.ImpactPoint.Z);
	const float Air = FMath::Clamp(static_cast<float>(Height / FadeHeight), 0.0f, 1.0f);
	const float Scale = FMath::Lerp(1.0f, AirScale, Air);
	SetCustomPrimitiveDataFloat(VrrBlobShadow::OpacityIndex, 1.0f - Air);

	// 4. Box on the ground, yawed with the owner, optionally tilted to the slope
	const FVector Up = bAlignToGround ? FVector(Hit.ImpactNormal) : FVector::UpVector;
	const FVector Forward = FVector::VectorPlaneProject(Owner->GetActorForwardVector(), Up).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const FRotator Rotation = FRotationMatrix::MakeFromXZ(Forward, Up).Rotator();
	const FVector BoxScale(
		SmoothedSize.X * Scale / VrrBlobShadow::CubeSize,
		SmoothedSize.Y * Scale / VrrBlobShadow::CubeSize,
		2.0f * ProjectionHalfHeight / VrrBlobShadow::CubeSize);
	SetWorldLocationAndRotation(Hit.ImpactPoint, Rotation);
	SetWorldScale3D(BoxScale);
}
