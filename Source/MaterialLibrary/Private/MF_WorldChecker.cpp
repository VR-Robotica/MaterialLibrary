// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Vrr World Checker: even world-space checker squares on any surface (0 on one set of squares, 1 on the other).
 *
 * Strategy
 *  A checker built from world X and Y alone stretches into stripes on walls, and floor() of a coordinate flips from
 *  frame to frame where a wall sits exactly on a tile boundary. Instead:
 *   - Box projection: the plane is picked from the normal's largest component (floors XY, walls YZ or XZ), so the
 *     axis the surface runs along is never used and every face gets square tiles.
 *   - The grid coordinate is folded to one period (two squares) with frac before anything else, so large world
 *     positions (large-world coordinates) become small, precise floats.
 *   - Filtered edges (Inigo Quilez's analytic box-filtered checker): each square's edge is blurred over the
 *     distance one screen pixel covers, so near squares get clean edges and far ones fade to 0.5 instead of shimmering.
 *
 * Steps
 *  1. Projection plane from the normal.
 *  2. Grid coordinate in squares, folded to 0-2, and squares per screen pixel (from the camera-relative position,
 *     which has derivatives).
 *  3. Filtered checker: i = 2 * (tri(p - w/2) - tri(p + w/2)) / w per axis, with tri(x) = |frac(x/2) - 0.5|,
 *     then 0.5 - 0.5 * i.x * i.y.
 *
 * Output
 *  0 Checker  0-1
 */

#include "MF_WorldChecker.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

UMaterialExpressionVrrWorldChecker::UMaterialExpressionVrrWorldChecker(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr World Checker"), { { TEXT("Checker"), MCT_Float1 } });
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrWorldChecker::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	const int32 One = Compiler->Constant(1.0f);
	const int32 Half = Compiler->Constant(0.5f);
	const int32 Two = Compiler->Constant(2.0f);

	// 1. Projection plane: the two axes the surface spreads along, picked from the normal's largest component
	const int32 N = Compiler->Abs(IsConnected(Normal) ? Compiler->ValidCast(Normal.Compile(Compiler), MCT_Float3) : Compiler->VertexNormal());
	const int32 NX = X(Compiler, N), NY = Y(Compiler, N), NZ = Z(Compiler, N);
	const int32 UseZ = Compiler->Step(Compiler->Max(NX, NY), NZ);						// floor / ceiling: XY plane
	const int32 UseX = Compiler->Mul(Compiler->Sub(One, UseZ), Compiler->Step(NY, NX));	// walls facing X: YZ plane
	const int32 AxisU = Compiler->Lerp(Compiler->Lerp(Compiler->Constant3(1, 0, 0), Compiler->Constant3(0, 1, 0), UseX), Compiler->Constant3(1, 0, 0), UseZ);
	const int32 AxisV = Compiler->Lerp(Compiler->Constant3(0, 0, 1), Compiler->Constant3(0, 1, 0), UseZ);

	// 2. Grid coordinate in squares. Folded to one period (0-2) first so a large-world position becomes a small float.
	const int32 InvTile = Compiler->Div(One, Compiler->Max(InputOr(Compiler, TileSizeIn, TileSize), Compiler->Constant(0.01f)));
	const int32 Position = IsConnected(WorldPositionIn) ? WorldPositionIn.Compile(Compiler) : Compiler->WorldPosition(WPT_Default);
	const int32 Squares = Compiler->Mul(Compiler->AppendVector(Compiler->Dot(Position, AxisU), Compiler->Dot(Position, AxisV)), InvTile);
	const int32 P = Compiler->Mul(Compiler->Frac(Compiler->Mul(Squares, Half)), Two);

	if (EdgeSoftness <= 0.0f)
	{
		// 3a. Hard edges: squares where floor(p.x) + floor(p.y) is odd
		const int32 Cells = Compiler->Floor(P);
		return Compiler->Mul(Compiler->Frac(Compiler->Mul(Compiler->Add(X(Compiler, Cells), Y(Compiler, Cells)), Half)), Two);
	}

	// 2a. Squares per screen pixel, from the camera-relative position (world positions have no derivatives).
	//     Clamped: at depth edges the derivative spikes, and a near-zero width would divide by zero.
	const int32 CameraRelative = Compiler->WorldPosition(WPT_CameraRelative);
	const int32 DerivSquares = Compiler->Mul(Compiler->AppendVector(Compiler->Dot(CameraRelative, AxisU), Compiler->Dot(CameraRelative, AxisV)), InvTile);
	const int32 Width = Compiler->Clamp(Compiler->Mul(PerPixelChange(Compiler, DerivSquares, 0.0f), Compiler->Constant(EdgeSoftness)),
		Compiler->Constant(0.001f), Two);

	// 3b. Box-filtered checker: the triangle wave tri(x) = |frac(x/2) - 0.5| integrates the square wave,
	//     so its difference across the pixel footprint over the width is the square wave's average there
	auto Tri = [&](int32 V) { return Compiler->Abs(Compiler->Sub(Compiler->Frac(Compiler->Mul(V, Half)), Half)); };
	const int32 HalfWidth = Compiler->Mul(Width, Half);
	const int32 I = Compiler->Div(Compiler->Mul(Compiler->Sub(Tri(Compiler->Sub(P, HalfWidth)), Tri(Compiler->Add(P, HalfWidth))), Two), Width);
	return Compiler->Sub(Half, Compiler->Mul(Half, Compiler->Mul(X(Compiler, I), Y(Compiler, I))));
}
#endif
