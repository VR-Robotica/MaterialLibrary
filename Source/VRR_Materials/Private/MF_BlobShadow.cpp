/*
 * Vrr Blob Shadow: a cel-shaded blob shadow, projected onto the ground like a decal.
 *
 * Strategy
 *  Toon floors are Unlit, and deferred decals only change GBuffer attributes, which Unlit surfaces ignore. So the
 *  shadow is a translucent box instead (drawn by UVrr_BlobShadowComponent): for each pixel the box covers,
 *  this node finds the scene point behind it from scene depth and draws the shadow if that point lies on the ground
 *  inside the box, so it wraps over uneven ground just like a decal, but changes the final color. The material
 *  blends toward SceneColor * ShadowColor rather than multiplying (see MF_BlobShadow.h), so blobs don't stack.
 *   - Scene point: camera and pixel are moved into box space first (a plain linear transform), then
 *       Ground = Camera + (Pixel - Camera) * SceneDepth / PixelDepth
 *     since both depths are view-space Z along the same ray. Box space is the 100 cm cube's local space / 50, so
 *     -1..1 on every axis whatever the component's scale.
 *   - Shape: a hard ellipse (r < 1) with a one-pixel anti-aliased edge (r measured in pixels via fwidth).
 *   - Hand-drawn wobble: the radius is pushed in and out by gradient noise sampled on the edge direction (so it
 *     only moves the outline, never punches holes) and on Time, which the material drives with the same stepped
 *     boil clock as the outline.
 *   - Receivers: only ground near the box's middle plane (fades toward top / bottom), only surfaces facing up the
 *     box (normal from the scene point's screen derivatives), and never Custom Depth objects (the owner), so feet
 *     and legs standing in the shadow aren't darkened.
 *   - Back faces only: the material is two sided with depth test off so it still works with the camera inside the
 *     box; skipping front faces keeps the shadow from being applied twice.
 *
 * Steps
 *  1. Camera and pixel in box space, scene point behind the pixel.
 *  2. Radius with the noise wobble, anti-aliased edge.
 *  3. Receiver masks: height, facing, Custom Depth, back face.
 *
 * Outputs
 *  0 Mask          0-1 shadow coverage
 *  1 Box Position  the scene point in box space (-1..1), for custom effects
 *  2 World Position  the scene point in world space, e.g. for Vrr Toon Shadow's World Position In, so the blob can
 *                    skip ground that's already in a toon cast shadow
 *  3 World Normal    the scene surface's normal (from screen derivatives), facing the camera
 */

#include "MF_BlobShadow.h"

#include "MaterialCompiler.h"
#include "MaterialSceneTextureId.h"
#include "VrrMaterialMath.h"

namespace VrrBlobShadow
{
	enum EOutput : int32 { Mask = 0, BoxPosition = 1, WorldPosition = 2, WorldNormal = 3 };

	/** Half the size of the engine's 100 cm cube: local space / this = box space (-1..1). */
	constexpr float HalfBoxSize = 50.0f;
}

UMaterialExpressionVrrBlobShadow::UMaterialExpressionVrrBlobShadow(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Blob Shadow"), {
		{ TEXT("Mask"), MCT_Float1 },
		{ TEXT("Box Position"), MCT_Float3 },
		{ TEXT("World Position"), MCT_Float3 },
		{ TEXT("World Normal"), MCT_Float3 }
	});
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrBlobShadow::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	const int32 One = Compiler->Constant(1.0f);
	const int32 Zero = Compiler->Constant(0.0f);

	// 1. Scene point behind the pixel, in box space. Both points go to the box's local space first (camera and pixel
	//    positions are large-world vectors; local space is small, so plain floats are fine from here on).
	auto ToBox = [&](int32 WorldPos)
	{
		const int32 Local = Compiler->ValidCast(Compiler->TransformPosition(MCB_World, MCB_Local, WorldPos), MCT_Float3);
		return Compiler->Div(Local, Compiler->Constant(VrrBlobShadow::HalfBoxSize));
	};
	const int32 CameraBox = ToBox(Compiler->ViewProperty(MEVP_WorldSpaceCameraPosition));
	const int32 PixelBox = ToBox(Compiler->WorldPosition(WPT_Default));
	const int32 SceneDepth = Compiler->SceneDepth(INDEX_NONE, INDEX_NONE, false);
	const int32 DepthRatio = Compiler->Div(SceneDepth, Compiler->Max(Compiler->PixelDepth(), Compiler->Constant(0.001f)));

	// World space: the same ray from the camera, kept relative to it (small floats) until the final add
	if (OutputIndex == VrrBlobShadow::WorldPosition || OutputIndex == VrrBlobShadow::WorldNormal)
	{
		const int32 CameraWorld = Compiler->ViewProperty(MEVP_WorldSpaceCameraPosition);
		const int32 ToPixel = Compiler->ValidCast(Compiler->Sub(Compiler->WorldPosition(WPT_Default), CameraWorld), MCT_Float3);
		const int32 ToGround = Compiler->Mul(ToPixel, DepthRatio);
		if (OutputIndex == VrrBlobShadow::WorldPosition)
		{
			return Compiler->Add(CameraWorld, ToGround);
		}
		// Flip it toward the camera: step(0, x) = 1 when it points away (dot with the camera-to-ground ray >= 0)
		const int32 Normal = Compiler->Normalize(Compiler->Cross(Compiler->DDX(ToGround), Compiler->DDY(ToGround)));
		const int32 Away = Compiler->Step(Zero, Compiler->Dot(Normal, ToGround));
		return Compiler->Mul(Normal, Compiler->Sub(One, Compiler->Mul(Away, Compiler->Constant(2.0f))));
	}
	const int32 Ground = Compiler->Add(CameraBox, Compiler->Mul(Compiler->Sub(PixelBox, CameraBox), DepthRatio));
	if (OutputIndex == VrrBlobShadow::BoxPosition)
	{
		return Ground;
	}

	// 2a. Radius on the box floor plane, pushed in and out by noise on the edge direction (and time)
	const int32 GroundXY = Compiler->ComponentMask(Ground, true, true, false, false);
	const int32 Radius = Compiler->Length(GroundXY);
	const int32 Direction = Compiler->Div(GroundXY, Compiler->Max(Radius, Compiler->Constant(0.0001f)));
	const int32 Time = IsConnected(TimeIn) ? TimeIn.Compile(Compiler) : Seconds(Compiler, false);
	const int32 NoisePos = Compiler->AppendVector(
		Compiler->Mul(Direction, InputOr(Compiler, NoiseScaleIn, NoiseScale)),
		Compiler->Mul(Compiler->ValidCast(Time, MCT_Float1), InputOr(Compiler, NoiseSpeedIn, NoiseSpeed)));
	const int32 Noise = GradientNoise(Compiler, NoisePos, 2);
	const int32 Wobbled = Compiler->Sub(Radius, Compiler->Mul(Noise, InputOr(Compiler, NoiseAmountIn, NoiseAmount)));

	// 2b. Hard edge, anti-aliased over EdgeSoftness pixels: (1 - r) / (r change per pixel), centered on the edge
	const int32 PerPixel = PerPixelChange(Compiler, Wobbled);
	const int32 Softness = Compiler->Max(InputOr(Compiler, EdgeSoftnessIn, EdgeSoftness), Compiler->Constant(0.1f));
	int32 Mask = Compiler->Saturate(Compiler->Add(
		Compiler->Div(Compiler->Sub(One, Wobbled), Compiler->Mul(PerPixel, Softness)), Compiler->Constant(0.5f)));

	// 3a. Height: full near the box's middle plane (the ground), fading out toward its top and bottom
	const float FadeRange = FMath::Max(1.0f - HeightFadeStart, 0.01f);
	Mask = Compiler->Mul(Mask, Compiler->Saturate(
		Compiler->Div(Compiler->Sub(One, Compiler->Abs(Z(Compiler, Ground))), Compiler->Constant(FadeRange))));

	// 3b. Facing: normal of the scene surface from its screen derivatives; ground faces along the box's Z
	const int32 Normal = Compiler->Normalize(Compiler->Cross(Compiler->DDX(Ground), Compiler->DDY(Ground)));
	const int32 Up = Compiler->Abs(Z(Compiler, Normal));
	Mask = Compiler->Mul(Mask, Compiler->SmoothStep(
		Compiler->Constant(FMath::Max(MinUpFacing - 0.1f, 0.0f)), Compiler->Constant(FMath::Min(MinUpFacing + 0.1f, 1.0f)), Up));

	// 3c. Custom Depth objects (the owner) are in front of the ground they stand on: skip their pixels
	if (bExcludeCustomDepth)
	{
		const int32 CustomDepth = X(Compiler, Compiler->SceneTextureLookup(INDEX_NONE, PPI_CustomDepth, false, false, false));
		const int32 Gap = Compiler->Sub(CustomDepth, SceneDepth);	// 0 on the object itself, large where there's none
		Mask = Compiler->Mul(Mask, Compiler->Saturate(Compiler->Div(Compiler->Sub(Gap, Compiler->Constant(1.0f)), Compiler->Constant(4.0f))));
	}

	// 3d. Back faces only (TwoSidedSign is -1 there), so the box doesn't apply the shadow twice
	Mask = Compiler->Mul(Mask, Compiler->Saturate(Compiler->Sub(Zero, Compiler->TwoSidedSign())));

	return Mask;
}
#endif
