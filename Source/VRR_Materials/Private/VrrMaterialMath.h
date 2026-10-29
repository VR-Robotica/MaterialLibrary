#pragma once

/*
 * Shared shader-building helpers for the VRR material nodes.
 *
 * How the nodes build shaders
 *  Every node is written with FMaterialCompiler ops (Add, Mul, Lerp...), not custom HLSL. Each op returns an int32
 *  "code chunk" handle, and the compiler turns the chain into HLSL. That keeps the nodes working with every shading
 *  path, lets constants fold away at compile time, and gives correct derivatives for free.
 *
 * What lives here
 *  - Inputs:       IsConnected / InputOr / Float3Or / ColorOr, for the "use the pin if wired, else the property"
 *                  pattern every node repeats (properties marked OverridingInputProperty).
 *  - Components:   X / Y / Z / W / XYZ masks, so long ComponentMask calls stay readable.
 *  - Math:         0-1 to -1..1, 2D rotation, change per screen pixel (fwidth), luminance, YIQ video color space,
 *                  hashes, value noise (deterministic, no sine, stable at large coordinates) and gradient noise.
 *  - Time:         seconds (game or real), stepped frame numbers and per-frame random seeds.
 *  - Post process: viewport UV, view size, "1080p pixel" units and clamped scene sampling.
 *
 * Post-process helpers sample PostProcessInput0 in viewport UV (0-1 over the view, not the whole buffer).
 * Editor only: material expressions only compile in the editor.
 */

#include "CoreMinimal.h"
#include "MaterialCompiler.h"
#include "MaterialSceneTextureId.h"
#include "Materials/MaterialExpressionNoise.h"

#if WITH_EDITOR

namespace VrrMaterialMath
{
	/** 0.618...: stepping by the golden ratio's fraction gives well spread pseudo-random values per frame. */
	constexpr float GoldenRatioFrac = 0.6180339887f;

	// INPUTS

	/** True when something is wired into the pin. */
	inline bool IsConnected(const FExpressionInput& Input)
	{
		return Input.GetTracedInput().Expression != nullptr;
	}

	/** The pin's value if connected, else the scalar Default (usually the matching property). */
	inline int32 InputOr(FMaterialCompiler* Compiler, FExpressionInput& Input, float Default)
	{
		return IsConnected(Input) ? Input.Compile(Compiler) : Compiler->Constant(Default);
	}

	/**
	 * The pin's value as a float3 if connected, else Default.
	 * Vector parameters (and Parameter Collection vectors) are float4, so the alpha is dropped;
	 * a scalar is spread to all three components.
	 */
	inline int32 Float3Or(FMaterialCompiler* Compiler, FExpressionInput& Input, const FVector3f& Default)
	{
		return IsConnected(Input)
			? Compiler->ValidCast(Input.Compile(Compiler), MCT_Float3)
			: Compiler->Constant3(Default.X, Default.Y, Default.Z);
	}

	/** Float3Or for colors: RGB of the pin if connected, else the property's RGB. */
	inline int32 ColorOr(FMaterialCompiler* Compiler, FExpressionInput& Input, const FLinearColor& Default)
	{
		return Float3Or(Compiler, Input, FVector3f(Default.R, Default.G, Default.B));
	}

	// COMPONENTS

	inline int32 X(FMaterialCompiler* Compiler, int32 V) { return Compiler->ComponentMask(V, true, false, false, false); }
	inline int32 Y(FMaterialCompiler* Compiler, int32 V) { return Compiler->ComponentMask(V, false, true, false, false); }
	inline int32 Z(FMaterialCompiler* Compiler, int32 V) { return Compiler->ComponentMask(V, false, false, true, false); }
	inline int32 W(FMaterialCompiler* Compiler, int32 V) { return Compiler->ComponentMask(V, false, false, false, true); }
	inline int32 XYZ(FMaterialCompiler* Compiler, int32 V) { return Compiler->ComponentMask(V, true, true, true, false); }

	// MATH

	/** 0-1 -> -1..1 (V * 2 - 1), e.g. for signed noise or random offsets. */
	inline int32 ToSigned(FMaterialCompiler* Compiler, int32 V01)
	{
		return Compiler->Sub(Compiler->Mul(V01, Compiler->Constant(2.0f)), Compiler->Constant(1.0f));
	}

	/** (X, Y) rotated by the angle whose cosine and sine are Cos and Sin: (X c - Y s, X s + Y c), as a float2. */
	inline int32 Rotate2D(FMaterialCompiler* Compiler, int32 X, int32 Y, int32 Cos, int32 Sin)
	{
		return Compiler->AppendVector(
			Compiler->Sub(Compiler->Mul(X, Cos), Compiler->Mul(Y, Sin)),
			Compiler->Add(Compiler->Mul(X, Sin), Compiler->Mul(Y, Cos)));
	}

	/**
	 * How much V changes from one screen pixel to the next: abs(ddx) + abs(ddy) (fwidth), never below MinChange.
	 * Divide a distance by it to measure that distance in screen pixels, e.g. for a one-pixel anti-aliased edge.
	 */
	inline int32 PerPixelChange(FMaterialCompiler* Compiler, int32 V, float MinChange = 1e-5f)
	{
		return Compiler->Max(Compiler->Add(Compiler->Abs(Compiler->DDX(V)), Compiler->Abs(Compiler->DDY(V))), Compiler->Constant(MinChange));
	}

	/** Rec. 709 luminance of a color. */
	inline int32 Luminance(FMaterialCompiler* Compiler, int32 Color)
	{
		return Compiler->Dot(XYZ(Compiler, Color), Compiler->Constant3(0.2126f, 0.7152f, 0.0722f));
	}

	/** YIQ (NTSC video) matrices, one row per output component: Y is brightness, I (orange-blue) and Q (purple-green) are color. */
	inline const FVector3f RGBToYIQRows[3] = { { 0.299f, 0.587f, 0.114f }, { 0.596f, -0.274f, -0.322f }, { 0.211f, -0.523f, 0.312f } };
	inline const FVector3f YIQToRGBRows[3] = { { 1.0f, 0.956f, 0.621f }, { 1.0f, -0.272f, -0.647f }, { 1.0f, -1.106f, 1.703f } };

	/** Rows (float3 dot products) applied to a float3: the shader side of a 3x3 matrix multiply. */
	inline int32 MulRows(FMaterialCompiler* Compiler, int32 V, const FVector3f (&Rows)[3])
	{
		auto Row = [&](int32 i) { return Compiler->Dot(V, Compiler->Constant3(Rows[i].X, Rows[i].Y, Rows[i].Z)); };
		return Compiler->AppendVector(Compiler->AppendVector(Row(0), Row(1)), Row(2));
	}

	/** RGB -> YIQ. */
	inline int32 ToYIQ(FMaterialCompiler* Compiler, int32 RGB) { return MulRows(Compiler, RGB, RGBToYIQRows); }

	/** YIQ -> RGB (inverse of ToYIQ). */
	inline int32 YIQToRGB(FMaterialCompiler* Compiler, int32 YIQ) { return MulRows(Compiler, YIQ, YIQToRGBRows); }

	/** The IQ (color) part of a YIQ value. */
	inline int32 IQ(FMaterialCompiler* Compiler, int32 YIQ)
	{
		return Compiler->ComponentMask(YIQ, false, true, true, false);
	}

	/** CPU-side YIQ of a color, for folding property colors into shader constants. */
	inline FVector3f ToYIQ(const FLinearColor& C)
	{
		const FVector3f RGB(C.R, C.G, C.B);
		return FVector3f(RGB | RGBToYIQRows[0], RGB | RGBToYIQRows[1], RGB | RGBToYIQRows[2]);
	}

	/**
	 * 0-1 hash of a float2 (Dave Hoskins' hash12). It uses no sine, so it stays stable for large pixel coordinates:
	 *   p3 = frac(p.xyx * 0.1031);  p3 += dot(p3, p3.yzx + 33.33);  return frac((p3.x + p3.y) * p3.z);
	 */
	inline int32 Hash12(FMaterialCompiler* Compiler, int32 P)
	{
		// 1. Spread the two coordinates over three components and scramble them with frac
		const int32 PX = X(Compiler, P);
		const int32 PY = Y(Compiler, P);
		int32 P3 = Compiler->Frac(Compiler->Mul(Compiler->AppendVector(Compiler->AppendVector(PX, PY), PX), Compiler->Constant(0.1031f)));

		// 2. Mix the components into each other (p3 += dot(p3, p3.yzx + 33.33))
		const int32 YZX = Compiler->AppendVector(Compiler->AppendVector(Y(Compiler, P3), Z(Compiler, P3)), X(Compiler, P3));
		P3 = Compiler->Add(P3, Compiler->Dot(P3, Compiler->Add(YZX, Compiler->Constant(33.33f))));

		// 3. Fold back to one 0-1 value
		return Compiler->Frac(Compiler->Mul(Compiler->Add(X(Compiler, P3), Y(Compiler, P3)), Z(Compiler, P3)));
	}

	/**
	 * Smooth 0-1 value noise on a float2: one random value per integer cell, blended between the cell's four
	 * corners with a smoothstep curve so there are no visible creases at the cell edges.
	 */
	inline int32 ValueNoise(FMaterialCompiler* Compiler, int32 P)
	{
		// 1. Which cell, and where inside it (0-1)
		const int32 Cell = Compiler->Floor(P);
		const int32 F = Compiler->Frac(P);

		// 2. Smoothstep weights: u = f * f * (3 - 2f)
		const int32 U = Compiler->Mul(Compiler->Mul(F, F), Compiler->Sub(Compiler->Constant(3.0f), Compiler->Mul(Compiler->Constant(2.0f), F)));

		// 3. Random value at each corner
		const int32 A = Hash12(Compiler, Cell);
		const int32 B = Hash12(Compiler, Compiler->Add(Cell, Compiler->Constant2(1.0f, 0.0f)));
		const int32 C = Hash12(Compiler, Compiler->Add(Cell, Compiler->Constant2(0.0f, 1.0f)));
		const int32 D = Hash12(Compiler, Compiler->Add(Cell, Compiler->Constant2(1.0f, 1.0f)));

		// 4. Blend along X on the top and bottom edges, then along Y between them
		const int32 UX = X(Compiler, U);
		return Compiler->Lerp(Compiler->Lerp(A, B, UX), Compiler->Lerp(C, D, UX), Y(Compiler, U));
	}

	/** -1..1 gradient noise (the Noise node's Gradient ALU) at a float3 position, with Levels octaves. */
	inline int32 GradientNoise(FMaterialCompiler* Compiler, int32 Position, int32 Levels = 1)
	{
		return Compiler->Noise(Position, EPositionOrigin::Absolute, 1.0f, 1, NOISEFUNCTION_GradientALU,
			false, Levels, -1.0f, 1.0f, 2.0f, Compiler->Constant(0.0f), false, 512);
	}

	/** Hash12 of (A, B + Salt): a 0-1 random value per A (e.g. a scanline) that changes with B (e.g. a frame seed). */
	inline int32 Hash2(FMaterialCompiler* Compiler, int32 A, int32 B, float Salt)
	{
		return Hash12(Compiler, Compiler->AppendVector(A, Compiler->Add(B, Compiler->Constant(Salt))));
	}

	// TIME

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

	/** frac(Frame * 0.618...): a 0-1 value that jumps to a new, well spread value every frame. */
	inline int32 FrameRandom(FMaterialCompiler* Compiler, int32 Frame)
	{
		return Compiler->Frac(Compiler->Mul(Frame, Compiler->Constant(GoldenRatioFrac)));
	}

	/** FrameRandom scaled to 0-1000: an offset for noise lookups (kept small for float precision). */
	inline int32 FrameSeed(FMaterialCompiler* Compiler, int32 Frame)
	{
		return Compiler->Mul(FrameRandom(Compiler, Frame), Compiler->Constant(1000.0f));
	}

	// POST PROCESS

	/** The pixel's viewport UV, or the UV input when connected. */
	inline int32 ViewportUV(FMaterialCompiler* Compiler, FExpressionInput& UVInput)
	{
		return IsConnected(UVInput) ? UVInput.Compile(Compiler) : Compiler->GetViewportUV();
	}

	/** View size in pixels (float2). */
	inline int32 ViewSize(FMaterialCompiler* Compiler)
	{
		return Compiler->GetSceneTextureViewSize(PPI_PostProcessInput0, false);
	}

	/**
	 * UV size of one pixel at 1080p, aspect-corrected, so offsets in "1080p pixels" look the same at any resolution:
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
		return XYZ(Compiler, Sample);
	}
}

#endif
