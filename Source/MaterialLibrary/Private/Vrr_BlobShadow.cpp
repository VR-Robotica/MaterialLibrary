// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

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
 *   - Air: the higher the owner is above the ground, the smaller and fainter the blob (Custom Primitive Data [CPD::BlobOpacity]).
 *   - Feet (Follow Feet, when the owner has foot bones): the blob is centered between the feet and its X axis runs
 *     from foot to foot. Width = the base width (capsule diameter for characters, since bounds already grow as the
 *     legs spread), length = width + the gap between the feet, so it's round with the feet together.
 *     The feet are smoothed as a center offset and a span vector; the span is flipped to match the last one first,
 *     so crossing feet (the line between them turning 180 degrees) doesn't spin the blob.
 *  It also runs in the editor (OnRegister + editor ticking), so the shadow shows while placing actors.
 *
 * Steps (every tick)
 *  1. Footprint of the owner's meshes (or capsule, when following feet), smoothed.
 *  2. Feet: center and span, smoothed.
 *  3. Trace down for the ground; hide when there's none.
 *  4. Height above ground -> size and opacity.
 *  5. Box transform and opacity.
 */

#include "Vrr_BlobShadow.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "VrrToonLighting.h"

namespace VrrBlobShadow
{
	/** The engine cube is 100 cm. */
	constexpr float CubeSize = 100.0f;

	/** Material used when none is assigned. */
	constexpr const TCHAR* DefaultMaterialPath = TEXT("/MaterialLibrary/Masters/Toon/M_Toon_BlobShadow.M_Toon_BlobShadow");

	/** Foot bone names tried when none are set (bone names match case-insensitively). */
	const TCHAR* const FootBoneNames[][2] =
	{
		{ TEXT("foot_l"), TEXT("foot_r") },					// UE mannequin, Paragon, BGC
		{ TEXT("L_foot"), TEXT("R_foot") },					// BGC Priss
		{ TEXT("LeftFoot"), TEXT("RightFoot") },			// Mixamo (renamed)
		{ TEXT("mixamorig:LeftFoot"), TEXT("mixamorig:RightFoot") },
	};
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

	SetDefaultCustomPrimitiveDataFloat(VrrToonLighting::CPD::BlobOpacity, 1.0f);
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
	bHasFeet = false;
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

bool UVrr_BlobShadowComponent::GetFeet(FVector& OutLeft, FVector& OutRight) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	// First visible skeletal mesh that has both feet (attachments like helmets have none)
	TInlineComponentArray<USkeletalMeshComponent*> Meshes(Owner);
	for (const USkeletalMeshComponent* Mesh : Meshes)
	{
		if (!Mesh->IsVisible() || !Mesh->IsRegistered() || !Mesh->GetSkeletalMeshAsset())
		{
			continue;
		}
		auto TryBones = [Mesh, &OutLeft, &OutRight](const FName Left, const FName Right)
		{
			const int32 LeftIndex = Mesh->GetBoneIndex(Left);
			const int32 RightIndex = Mesh->GetBoneIndex(Right);
			if (LeftIndex == INDEX_NONE || RightIndex == INDEX_NONE)
			{
				return false;
			}
			OutLeft = Mesh->GetBoneTransform(LeftIndex).GetLocation();
			OutRight = Mesh->GetBoneTransform(RightIndex).GetLocation();
			return true;
		};
		if (!LeftFootBone.IsNone() || !RightFootBone.IsNone())
		{
			if (TryBones(LeftFootBone, RightFootBone))
			{
				return true;
			}
			continue;
		}
		for (const auto& Names : VrrBlobShadow::FootBoneNames)
		{
			if (TryBones(Names[0], Names[1]))
			{
				return true;
			}
		}
	}
	return false;
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
	FVector LeftFoot;
	FVector RightFoot;
	const bool bFeet = bFollowFeet && GetFeet(LeftFoot, RightFoot);
	const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Owner->GetRootComponent());
	if (FixedWidth > 0.0f)
	{
		Size = FVector2D(FixedWidth, FixedWidth);
	}
	else if (bFeet && Capsule)
	{
		// The feet add the length; the capsule gives a width that doesn't change as the legs spread
		const double Diameter = 2.0 * Capsule->GetScaledCapsuleRadius() * WidthScale;
		Size = FVector2D(Diameter, Diameter);
	}
	else
	{
		if (bRound || bFeet)
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
	FVector2D BlobSize = SmoothedSize;

	// 2. Feet: centered between them, stretched along the line from one to the other
	if (bFeet)
	{
		const FVector2D ActorXY(Owner->GetActorLocation());
		const FVector2D Center = 0.5 * (FVector2D(LeftFoot) + FVector2D(RightFoot)) - ActorXY;
		FVector2D Span = FVector2D(RightFoot) - FVector2D(LeftFoot);
		if (!bHasFeet || FootSmoothing <= 0.0f || DeltaSeconds <= 0.0f)
		{
			SmoothedFeetCenter = Center;
			SmoothedFeetSpan = Span;
		}
		else
		{
			// An ellipse looks the same turned 180 degrees: follow the closer of the two directions
			if ((Span | SmoothedFeetSpan) < 0.0)
			{
				Span = -Span;
			}
			SmoothedFeetCenter = FMath::Vector2DInterpTo(SmoothedFeetCenter, Center, DeltaSeconds, FootSmoothing);
			SmoothedFeetSpan = FMath::Vector2DInterpTo(SmoothedFeetSpan, Span, DeltaSeconds, FootSmoothing);
		}
		bHasFeet = true;

		const FVector2D TraceXY = ActorXY + SmoothedFeetCenter;
		TraceStart.X = TraceXY.X;
		TraceStart.Y = TraceXY.Y;
		BlobSize.X += SmoothedFeetSpan.Size() * FootSpreadScale;
	}
	else
	{
		bHasFeet = false;
	}

	// 3. Ground below the owner (the owner itself is ignored)
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

	// 4. Height of the owner's lowest point above the ground: shrink and fade while in the air
	const double Height = FMath::Max(0.0, Bottom.Z - Hit.ImpactPoint.Z);
	const float Air = FMath::Clamp(static_cast<float>(Height / FadeHeight), 0.0f, 1.0f);
	const float Scale = FMath::Lerp(1.0f, AirScale, Air);
	const float Opacity = 1.0f - Air;
	const TArray<float>& Data = GetCustomPrimitiveData().Data;
	if (!Data.IsValidIndex(VrrToonLighting::CPD::BlobOpacity) || Data[VrrToonLighting::CPD::BlobOpacity] != Opacity)
	{
		SetCustomPrimitiveDataFloat(VrrToonLighting::CPD::BlobOpacity, Opacity);	// only on change: each write re-uploads it
	}

	// 5. Box on the ground, optionally tilted to the slope. X runs foot to foot (owner's right with the feet together),
	//    or along the owner's forward without feet
	const FVector Up = bAlignToGround ? FVector(Hit.ImpactNormal) : FVector::UpVector;
	const FVector OwnerAxis = bFeet ? Owner->GetActorRightVector() : Owner->GetActorForwardVector();
	FVector Axis = bFeet ? FVector(SmoothedFeetSpan, 0.0) : OwnerAxis;
	Axis = FVector::VectorPlaneProject(Axis, Up);
	if (!Axis.Normalize(1.0))	// feet (nearly) on top of each other: no direction to follow
	{
		Axis = FVector::VectorPlaneProject(OwnerAxis, Up).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	}
	const FRotator Rotation = FRotationMatrix::MakeFromXZ(Axis, Up).Rotator();
	const FVector BoxScale(
		BlobSize.X * Scale / VrrBlobShadow::CubeSize,
		BlobSize.Y * Scale / VrrBlobShadow::CubeSize,
		2.0f * ProjectionHalfHeight / VrrBlobShadow::CubeSize);
	SetWorldTransform(FTransform(Rotation, Hit.ImpactPoint, BoxScale));	// one transform update, not two
}
