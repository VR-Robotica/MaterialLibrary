// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Vrr_CelLayer.generated.h"

class UPrimitiveComponent;

namespace VrrCelLayer
{
	/**
	 * Custom Stencil bit that marks cel layer meshes. Only this bit is written (stencil write mask), so other
	 * stencil uses can keep the remaining bits. Vrr Cel Shadow reads the same bit.
	 */
	constexpr int32 StencilBit = 128;
}

/**
 * Makes its owner a "cel layer": it draws like an animation cel lying on top of the background, with an offset
 * shadow showing through the gap underneath (Vrr Cel Shadow in the M_PP_CelShadow post process material).
 *
 * The component only tags the owner's meshes: Custom Depth on, plus the cel stencil bit. The look lives in the
 * post process. Needs Project Settings > Rendering > Custom Depth-Stencil Pass = "Enabled with Stencil".
 */
UCLASS(ClassGroup = (Rendering), meta = (BlueprintSpawnableComponent, DisplayName = "Vrr Cel Layer"))
class MATERIALLIBRARY_API UVrr_CelLayerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVrr_CelLayerComponent();

	/** Off: the owner's meshes lose the cel stencil bit (Custom Depth stays on, other effects may use it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cel Layer")
	bool bEnabled = true;

	/** Turns the cel layer on or off for the owner's meshes. */
	UFUNCTION(BlueprintCallable, Category = "Cel Layer")
	void SetCelLayerEnabled(bool bNewEnabled);

	/** Tags the owner's meshes now. Also runs every tick, so meshes created later are picked up on their own. */
	UFUNCTION(BlueprintCallable, Category = "Cel Layer")
	void Refresh();

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** True for the owner's meshes that belong to the cel (skips helper meshes like the blob shadow box). */
	static bool IsCelMesh(const UPrimitiveComponent* Primitive);
};
