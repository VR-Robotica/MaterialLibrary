#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Vrr_BlobShadow.generated.h"

/**
 * Cel-shaded blob shadow under its owner (a dark, slightly wobbly ellipse like hand-drawn animation).
 *
 * A 100 cm cube with M_Toon_BlobShadow (Vrr Blob Shadow node) that the component keeps on the ground below the
 * owner every frame: a downward trace finds the ground, the box is centered there (tilted to the slope), and its
 * width follows the owner's meshes (their combined bounds, smoothed so animation doesn't make it pulse). While the
 * owner is in the air the blob shrinks and fades, and it hides when there's no ground in range.
 *
 * Characters (Follow Feet): the blob sits between the feet and stretches along the line between them, so it's a
 * circle with the feet together and an ellipse as they spread. Its width then comes from the capsule, which
 * doesn't change with the animation.
 *
 *   Custom Primitive Data [CPD::BlobOpacity] (VrrToonLighting.h) = opacity (air fade), read by the material.
 *   The owner's meshes render Custom Depth (optional) so the material leaves them out of their own shadow.
 */
UCLASS(ClassGroup = (Rendering), meta = (BlueprintSpawnableComponent, DisplayName = "Vrr Blob Shadow"))
class VRR_MATERIALS_API UVrr_BlobShadowComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UVrr_BlobShadowComponent();

	/** Blob width relative to the owner's mesh width (with Follow Feet on a character: the capsule's width). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow", meta = (ClampMin = "0.0", UIMax = "3.0"))
	float WidthScale = 1.0f;

	/** Fixed blob width in cm. 0 = follow the owner's mesh width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow", meta = (ClampMin = "0.0"))
	float FixedWidth = 0.0f;

	/** On: a circle sized by the average of the owner's length and width. Off: an ellipse following both. Not used while following feet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow")
	bool bRound = true;

	/**
	 * Shape the blob from the owner's feet: centered between them and stretched along the line between them,
	 * round with the feet together, elliptical as they move apart. Needs a skeletal mesh with foot bones;
	 * without them the blob follows the owner's bounds as usual.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow|Feet")
	bool bFollowFeet = true;

	/** Left foot bone. None = find it (foot_l, L_foot, LeftFoot...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow|Feet", meta = (EditCondition = "bFollowFeet"))
	FName LeftFootBone;

	/** Right foot bone. None = find it (foot_r, R_foot, RightFoot...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow|Feet", meta = (EditCondition = "bFollowFeet"))
	FName RightFootBone;

	/** How much the gap between the feet stretches the blob: 1 = its length grows by the full gap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow|Feet", meta = (EditCondition = "bFollowFeet", ClampMin = "0.0", UIMax = "3.0"))
	float FootSpreadScale = 1.0f;

	/** How fast the blob follows the feet (per second). Higher keeps up with fast steps; 0 = instantly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow|Feet", meta = (EditCondition = "bFollowFeet", ClampMin = "0.0"))
	float FootSmoothing = 20.0f;

	/** Half height of the projection box in cm: ground this far above or below the trace hit still receives it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow", meta = (ClampMin = "1.0", UIMax = "100.0"))
	float ProjectionHalfHeight = 25.0f;

	/** How far below the owner to look for ground, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow", meta = (ClampMin = "0.0"))
	float MaxGroundDistance = 500.0f;

	/** Height above the ground (cm) where the blob has faded out completely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow", meta = (ClampMin = "1.0"))
	float FadeHeight = 300.0f;

	/** Blob size at FadeHeight, relative to its size on the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirScale = 0.5f;

	/** Tilt the blob to the ground's slope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow")
	bool bAlignToGround = true;

	/** How fast the size follows the owner's bounds (per second). 0 = instantly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow", meta = (ClampMin = "0.0"))
	float SizeSmoothing = 6.0f;

	/** Turn on Custom Depth for the owner's meshes, so the shadow doesn't darken the owner standing in it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow")
	bool bExcludeOwner = true;

	/** Places and sizes the blob now (it also runs every tick). */
	UFUNCTION(BlueprintCallable, Category = "Blob Shadow")
	void UpdateBlob(float DeltaSeconds = 0.0f);

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Owner's visible meshes: top and bottom center in world space, and size (XY, cm, in actor space). */
	bool GetOwnerFootprint(FVector& OutTop, FVector& OutBottom, FVector2D& OutSize) const;

	/** World positions of the owner's left and right foot bones, if it has a skeletal mesh with them. */
	bool GetFeet(FVector& OutLeft, FVector& OutRight) const;

	/** Smoothed footprint size; negative until the first update. */
	FVector2D SmoothedSize = FVector2D(-1.0, -1.0);

	/** Smoothed feet, in world XY: the point between them relative to the owner, and left-to-right foot. */
	FVector2D SmoothedFeetCenter = FVector2D::ZeroVector;
	FVector2D SmoothedFeetSpan = FVector2D::ZeroVector;
	bool bHasFeet = false;
};
