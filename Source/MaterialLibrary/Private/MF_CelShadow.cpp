// Copyright VR Robotica. All Rights Reserved.

/*
 * Vrr Cel Shadow: a flat drop shadow behind cel layer meshes, like an animation cel lying a little above the
 * background. Use in a Post Process material at "Scene Color Before DOF".
 *
 * Strategy
 *  Vrr Cel Layer marks the cel meshes: they render Custom Depth and set VrrCelLayer::StencilBit in Custom Stencil.
 *  A shadow is the cel's silhouette moved by Offset, so each pixel looks back by Offset (a gather: one lookup per
 *  pixel, whatever the number of characters) and asks whether a cel is there. If so, this pixel is in the shadow,
 *  unless it is in front of the cel:
 *   - Depth test: this pixel's scene depth against the cel's Custom Depth at the source pixel. Surfaces in front of
 *     the cel (foreground props, walls between it and the camera) stay clear, with Ground Tolerance of slack so the
 *     ground right at its feet still gets the shadow.
 *   - Cel on cel: when this pixel shows a cel as well, the shadow needs Layer Gap of depth between the two. Parts of
 *     one character are close in depth, so it never shadows itself, but a character further back does get it.
 *   - The character itself is never darkened by its own shadow: its pixels are a cel at the same depth.
 *  The shadow is flat (one color, one opacity), so it reads as a painted layer rather than lighting.
 *  Stencil lookups are point sampled, so the edge is hard; the material runs before TSR, which anti-aliases it
 *  (r.CustomDepthTemporalAAJitter keeps Custom Depth jittered like the scene).
 *
 * Steps
 *  1. Source pixel (this pixel - Offset).
 *  2. Is the source a cel, and how deep is it.
 *  3. Is this pixel a cel (visible cel stencil at its own Custom Depth).
 *  4. Depth tests -> Mask.
 *  5. Blend the shadow color over the scene.
 *
 * Outputs
 *  0 Color  scene color with the shadow
 *  1 Mask   0-1 shadow coverage (before Opacity)
 */

#include "MF_CelShadow.h"

#include "MaterialCompiler.h"
#include "MaterialSceneTextureId.h"
#include "VrrMaterialMath.h"
#include "Vrr_CelLayer.h"

namespace VrrCelShadow
{
	enum EOutput : int32 { Color = 0, Mask = 1 };

	/** Depth slack (cm) for "this pixel shows the Custom Depth object": both depths come from the same mesh. */
	constexpr float SameSurfaceSlack = 1.0f;
}

UMaterialExpressionVrrCelShadow::UMaterialExpressionVrrCelShadow(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Cel Shadow"), { { TEXT("Color"), MCT_Float3 }, { TEXT("Mask"), MCT_Float1 } });
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrCelShadow::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	const int32 One = Compiler->Constant(1.0f);
	const int32 BaseUV = ViewportUV(Compiler, UV);

	// Scene texture read at a viewport UV, point sampled (stencil and depth must not be blended across edges)
	auto Lookup = [&](int32 AtUV, ESceneTextureId Id)
	{
		return X(Compiler, Compiler->SceneTextureLookup(AtUV, Id, /*bFiltered=*/ false, /*bClamped=*/ false, /*bUnused=*/ false));
	};

	// Stencil value -> 1 when the cel bit is set: floor(s / bit) is odd
	auto HasCelBit = [&](int32 Stencil)
	{
		const int32 Shifted = Compiler->Floor(Compiler->Div(Stencil, Compiler->Constant(static_cast<float>(VrrCelLayer::StencilBit))));
		return Compiler->Fmod(Shifted, Compiler->Constant(2.0f));
	};

	// 1. Source pixel: the shadow is the cel moved by Offset, so look back by Offset (1080p pixels -> UV)
	const int32 OffsetPx = IsConnected(OffsetIn)
		? Compiler->ValidCast(OffsetIn.Compile(Compiler), MCT_Float2)
		: Compiler->Constant2(static_cast<float>(Offset.X), static_cast<float>(Offset.Y));
	const int32 SourceUV = Compiler->Saturate(Compiler->Sub(BaseUV, Compiler->Mul(OffsetPx, PixelAt1080(Compiler))));

	// 2. Is there a cel at the source pixel, and at what depth
	const int32 SourceIsCel = HasCelBit(Lookup(SourceUV, PPI_CustomStencil));
	const int32 SourceDepth = Lookup(SourceUV, PPI_CustomDepth);

	// 3. Is this pixel a cel: cel bit set, and the visible surface is the Custom Depth object (nothing in front of it)
	const int32 SceneDepth = Lookup(BaseUV, PPI_SceneDepth);
	const int32 HereDepth = Lookup(BaseUV, PPI_CustomDepth);
	const int32 HereIsCel = Compiler->Mul(
		HasCelBit(Lookup(BaseUV, PPI_CustomStencil)),
		Compiler->Step(HereDepth, Compiler->Add(SceneDepth, Compiler->Constant(VrrCelShadow::SameSurfaceSlack))));

	// 4a. Not in front of the cel (with Ground Tolerance of slack): step(edge, x) = x >= edge
	const int32 NotInFront = Compiler->Step(
		Compiler->Sub(SourceDepth, Compiler->Constant(GroundTolerance)), SceneDepth);

	// 4b. On another cel, only when it lies at least Layer Gap behind the caster
	const int32 BehindLayer = Compiler->Step(
		Compiler->Add(SourceDepth, Compiler->Constant(LayerGap)), SceneDepth);
	const int32 Receives = Compiler->Lerp(One, BehindLayer, HereIsCel);

	const int32 Mask = Compiler->Mul(Compiler->Mul(SourceIsCel, NotInFront), Receives);
	if (OutputIndex == VrrCelShadow::Mask)
	{
		return Mask;
	}

	// 5. Flat shadow: lerp toward the shadow color by Mask * Opacity
	const int32 Scene = IsConnected(SceneColorIn)
		? Compiler->ValidCast(SceneColorIn.Compile(Compiler), MCT_Float3)
		: SampleScene(Compiler, BaseUV);
	const int32 Alpha = Compiler->Mul(Mask, Compiler->Saturate(InputOr(Compiler, OpacityIn, Opacity)));
	return Compiler->Lerp(Scene, ColorOr(Compiler, ShadowColorIn, ShadowColor), Alpha);
}
#endif
