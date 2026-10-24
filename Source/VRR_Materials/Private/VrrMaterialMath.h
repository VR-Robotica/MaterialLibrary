#pragma once

#include "CoreMinimal.h"
#include "MaterialCompiler.h"
#include "MaterialSceneTextureId.h"

#if WITH_EDITOR

/**
 * Shared shader-building helpers for the VRR material nodes (all built from FMaterialCompiler ops, no custom HLSL).
 * Post-process helpers sample PostProcessInput0 in viewport UV (0-1 over the view, not the whole buffer).
 */
namespace VrrMaterialMath
{
	/** Optional input if connected, else a scalar constant. */
	inline int32 InputOr(FMaterialCompiler* Compiler, FExpressionInput& Input, float Default)
	{
		return Input.GetTracedInput().Expression ? Input.Compile(Compiler) : Compiler->Constant(Default);
	}

	inline int32 X(FMaterialCompiler* Compiler, int32 V) { return Compiler->ComponentMask(V, true, false, false, false); }
	inline int32 Y(FMaterialCompiler* Compiler, int32 V) { return Compiler->ComponentMask(V, false, true, false, false); }
	inline int32 Z(FMaterialCompiler* Compiler, int32 V) { return Compiler->ComponentMask(V, false, false, true, false); }

	/** Rec. 709 luminance of a color. */
	inline int32 Luminance(FMaterialCompiler* Compiler, int32 Color)
	{
		return Compiler->Dot(Compiler->ComponentMask(Color, true, true, true, false), Compiler->Constant3(0.2126f, 0.7152f, 0.0722f));
	}

	/**
	 * 0-1 hash of a float2 (Dave Hoskins' hash12, no sine, so it stays stable for large pixel coordinates):
	 *   p3 = frac(p.xyx * 0.1031);  p3 += dot(p3, p3.yzx + 33.33);  return frac((p3.x + p3.y) * p3.z);
	 */
	inline int32 Hash12(FMaterialCompiler* Compiler, int32 P)
	{
		const int32 PX = X(Compiler, P);
		const int32 PY = Y(Compiler, P);
		int32 P3 = Compiler->Frac(Compiler->Mul(Compiler->AppendVector(Compiler->AppendVector(PX, PY), PX), Compiler->Constant(0.1031f)));
		const int32 YZX = Compiler->AppendVector(Compiler->AppendVector(Y(Compiler, P3), Z(Compiler, P3)), X(Compiler, P3));
		P3 = Compiler->Add(P3, Compiler->Dot(P3, Compiler->Add(YZX, Compiler->Constant(33.33f))));
		return Compiler->Frac(Compiler->Mul(Compiler->Add(X(Compiler, P3), Y(Compiler, P3)), Z(Compiler, P3)));
	}

	/** Smooth 0-1 value noise on a float2 (one random value per integer cell, smoothstep-blended between the four corners). */
	inline int32 ValueNoise(FMaterialCompiler* Compiler, int32 P)
	{
		const int32 Cell = Compiler->Floor(P);
		const int32 F = Compiler->Frac(P);
		// u = f * f * (3 - 2f)
		const int32 U = Compiler->Mul(Compiler->Mul(F, F), Compiler->Sub(Compiler->Constant(3.0f), Compiler->Mul(Compiler->Constant(2.0f), F)));
		const int32 A = Hash12(Compiler, Cell);
		const int32 B = Hash12(Compiler, Compiler->Add(Cell, Compiler->Constant2(1.0f, 0.0f)));
		const int32 C = Hash12(Compiler, Compiler->Add(Cell, Compiler->Constant2(0.0f, 1.0f)));
		const int32 D = Hash12(Compiler, Compiler->Add(Cell, Compiler->Constant2(1.0f, 1.0f)));
		const int32 UX = X(Compiler, U);
		return Compiler->Lerp(Compiler->Lerp(A, B, UX), Compiler->Lerp(C, D, UX), Y(Compiler, U));
	}

	/** Seconds: real time (keeps running while paused) or game time. */
	inline int32 Seconds(FMaterialCompiler* Compiler, bool bIgnorePause)
	{
		return bIgnorePause ? Compiler->RealTime(false, 0.0f) : Compiler->GameTime(false, 0.0f);
	}

	/** floor(Seconds * Rate): the index of the current held frame. */
	inline int32 SteppedFrame(FMaterialCompiler* Compiler, float Rate, bool bIgnorePause)
	{
		return Compiler->Floor(Compiler->Mul(Seconds(Compiler, bIgnorePause), Compiler->Constant(FMath::Max(Rate, 0.1f))));
	}

	/** A 0-1000 offset that jumps to a new pseudo-random value every frame (golden ratio sequence; small for float precision). */
	inline int32 FrameSeed(FMaterialCompiler* Compiler, int32 Frame)
	{
		return Compiler->Mul(Compiler->Frac(Compiler->Mul(Frame, Compiler->Constant(0.6180339887f))), Compiler->Constant(1000.0f));
	}

	/** Hash12 of (A, B + Salt): a 0-1 random value per A (e.g. a scanline) that changes with B (e.g. a frame seed). */
	inline int32 Hash2(FMaterialCompiler* Compiler, int32 A, int32 B, float Salt)
	{
		return Hash12(Compiler, Compiler->AppendVector(A, Compiler->Add(B, Compiler->Constant(Salt))));
	}

	/** The pixel's viewport UV, or the UV input when connected. */
	inline int32 ViewportUV(FMaterialCompiler* Compiler, FExpressionInput& UVInput)
	{
		return UVInput.GetTracedInput().Expression ? UVInput.Compile(Compiler) : Compiler->GetViewportUV();
	}

	/** View size in pixels (float2). */
	inline int32 ViewSize(FMaterialCompiler* Compiler)
	{
		return Compiler->GetSceneTextureViewSize(PPI_PostProcessInput0, false);
	}

	/**
	 * UV size of one pixel at 1080p, aspect-corrected: offsets in "1080p pixels" look the same at any resolution.
	 *   (H / W / 1080, 1 / 1080)
	 */
	inline int32 PixelAt1080(FMaterialCompiler* Compiler)
	{
		const int32 Size = ViewSize(Compiler);
		const int32 InvHeight = Compiler->Constant(1.0f / 1080.0f);
		return Compiler->AppendVector(Compiler->Mul(Compiler->Div(Y(Compiler, Size), X(Compiler, Size)), InvHeight), InvHeight);
	}

	/** PostProcessInput0 RGB at a viewport UV, clamped to the view so edge taps don't read outside it. */
	inline int32 SampleScene(FMaterialCompiler* Compiler, int32 UV)
	{
		const int32 Clamped = Compiler->Saturate(UV);
		const int32 Sample = Compiler->SceneTextureLookup(Clamped, PPI_PostProcessInput0, /*bFiltered=*/ true, /*bClamped=*/ false, /*bUnused=*/ false);
		return Compiler->ComponentMask(Sample, true, true, true, false);
	}
}

#endif
