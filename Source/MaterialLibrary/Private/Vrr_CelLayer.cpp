// Copyright VR Robotica. All Rights Reserved.

/*
 * Vrr Cel Layer component: tags its owner's meshes for the cel shadow post process.
 *
 * Strategy
 *  The cel look is a screen-space composite, so the component never draws anything itself. It marks which pixels
 *  belong to a cel: every mesh on the owner renders Custom Depth and writes VrrCelLayer::StencilBit into Custom
 *  Stencil. Only that bit is written (write mask), so the value never clobbers other stencil users, and Custom
 *  Depth stays shared with effects that already use it (the blob shadow turns it on for the same meshes).
 *  Vrr Cel Shadow then finds the cel pixels from the stencil and their depth from Custom Depth.
 *  Many owners create their meshes after this component registers (construction scripts, attached-mesh
 *  components, runtime spawns), and a construction script rerun replaces them all. So tagging also runs every
 *  tick, in game and in the editor: it only touches a mesh whose settings are wrong, so once everything is tagged
 *  a tick is a short loop over the owner's components.
 *
 * Steps (Refresh)
 *  1. Find the owner's meshes, skipping helper meshes.
 *  2. Custom Depth on, stencil bit set (or cleared when disabled), only where it differs.
 */

#include "Vrr_CelLayer.h"

#include "Components/MeshComponent.h"
#include "GameFramework/Actor.h"
#include "Vrr_BlobShadow.h"

UVrr_CelLayerComponent::UVrr_CelLayerComponent()
{
	// Picks up meshes created after registration (see Strategy)
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;	// after anything this frame that spawned meshes
	bTickInEditor = true;
}

void UVrr_CelLayerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Refresh();
}

void UVrr_CelLayerComponent::OnRegister()
{
	Super::OnRegister();
	Refresh();
}

void UVrr_CelLayerComponent::BeginPlay()
{
	Super::BeginPlay();
	Refresh();
}

void UVrr_CelLayerComponent::SetCelLayerEnabled(bool bNewEnabled)
{
	bEnabled = bNewEnabled;
	Refresh();
}

void UVrr_CelLayerComponent::Refresh()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// 1. The owner's meshes
	TInlineComponentArray<UMeshComponent*> Meshes(Owner);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!IsCelMesh(Mesh))
		{
			continue;
		}

		// 2. Custom Depth on; write only the cel bit, set or cleared. Skip meshes already right (runs every tick).
		const int32 Stencil = bEnabled ? VrrCelLayer::StencilBit : 0;
		if (!Mesh->bRenderCustomDepth)
		{
			Mesh->SetRenderCustomDepth(true);
		}
		if (Mesh->CustomDepthStencilWriteMask != ERendererStencilMask::ERSM_128)
		{
			Mesh->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_128);
		}
		if (Mesh->CustomDepthStencilValue != Stencil)
		{
			Mesh->SetCustomDepthStencilValue(Stencil);
		}
	}
}

bool UVrr_CelLayerComponent::IsCelMesh(const UPrimitiveComponent* Primitive)
{
	// The blob shadow box is a translucent helper on the ground, not part of the character
	return Primitive && !Primitive->IsA<UVrr_BlobShadowComponent>();
}
