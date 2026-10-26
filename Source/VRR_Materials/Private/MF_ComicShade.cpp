/*
 * Vrr Comic Shade: comic print shading (hatching + halftone dots) for a Post Process material.
 *
 * Strategy
 *  Both patterns are drawn from a 2D grid coordinate measured in "cells" (one line or one dot per cell):
 *   - Screen space: 1080p pixels, so the pattern is fixed to the screen like a printed page.
 *   - World space:  the world position projected onto the plane of the surface's main axis (triplanar-style, one
 *                   plane picked per pixel), so the pattern sticks to surfaces. The normal comes from depth
 *                   derivatives, since Unlit toon materials don't write usable G-buffer normals.
 *  Within a cell, frac() of the coordinate says where the pixel is, and the shape test is "is this pixel inside the
 *  line / dot of the size this tone asks for?". Every edge is anti-aliased: the inside test ramps over the width of
 *  one screen pixel, measured from screen derivatives, so lines and dots stay crisp at any spacing or distance.
 *
 *  Tone (0 dark - 1 bright) decides the sizes:
 *   - Hatching: below Shadow Level, lines thicken the darker it gets; a second crossed set joins in the darkest part.
 *   - Halftone (by Dot Style):
 *       Highlights  light dots above Highlight Level that grow with brightness.
 *       Print       classic ink dots that grow as it gets darker, touching at mid-gray and leaving paper holes.
 *       Hi-Fi Rush  dots between Shadow Level and Highlight Level, so light falloff reads as dot size; dots brighten
 *                   and gaps darken by their covered area, keeping the average brightness the same.
 *  Midtones stay clean, and both patterns fade out with distance (the sky is always past the fade).
 *
 * Steps
 *  1. Pattern space (screen pixels, or world plane axes).
 *  2. MakeGrid(): rotated, scaled grid coordinate + anti-aliasing width.
 *  3. Hatching mask.
 *  4. Halftone mask (one branch per Dot Style).
 *  5. Distance fade.
 *  6. Composite onto Color.
 *
 * Outputs
 *  0 Color  the shaded picture
 *  1 Hatch  0-1 line mask
 *  2 Dots   0-1 dot mask
 */

#include "MF_ComicShade.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

namespace VrrComicShade
{
	enum EOutput : int32 { Color = 0, Hatch = 1, Dots = 2 };

	/** How fast a pattern switches fully on past its threshold (1/20 of the tone range), so no hairline shows at it. */
	constexpr float GateSharpness = 20.0f;

	/** A pattern grid: where the pixel is in cells, and how many cells one screen pixel spans. */
	struct FGrid
	{
		int32 Coord;	// float2 in cells: frac() gives the position inside a cell
		int32 Width;	// cells per screen pixel: the anti-aliasing width
	};
}

UMaterialExpressionVrrComicShade::UMaterialExpressionVrrComicShade(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitNode(TEXT("Vrr Comic Shade"), { TEXT("Color"), TEXT("Hatch"), TEXT("Dots") });
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrComicShade::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;
	using VrrComicShade::FGrid;
	using VrrComicShade::GateSharpness;

	if (!IsConnected(Color))
	{
		return MissingInput(Compiler, TEXT("Color"));
	}
	const int32 Zero = Compiler->Constant(0.0f);
	const int32 One = Compiler->Constant(1.0f);
	const int32 Half = Compiler->Constant(0.5f);
	const int32 ColorArg = Compiler->ValidCast(Color.Compile(Compiler), MCT_Float3);

	// 1. Pattern space. Each pattern needs a 2D grid coordinate (in cells) for the shape, and the same coordinate built
	//    from plain floats for screen derivatives (world positions are large-world types, which have no derivatives).
	int32 PatternU = INDEX_NONE, PatternV = INDEX_NONE;		// World: the projection plane's axes (float3)
	int32 ScreenPx = INDEX_NONE;							// Screen: 1080p pixel position
	int32 WorldPos = INDEX_NONE, CameraRelativePos = INDEX_NONE;
	if (Space == EVrrComicSpace::World)
	{
		WorldPos = Compiler->WorldPosition(WPT_Default);
		CameraRelativePos = Compiler->WorldPosition(WPT_CameraRelative);

		// 1a. Normal from depth (cross of the position's screen derivatives), absolute so only the axis matters
		const int32 Normal = Compiler->Abs(Compiler->Cross(Compiler->DDX(CameraRelativePos), Compiler->DDY(CameraRelativePos)));
		const int32 NX = X(Compiler, Normal), NY = Y(Compiler, Normal), NZ = Z(Compiler, Normal);

		// 1b. Pick the plane facing the largest normal component: floors/ceilings use XY, walls YZ or XZ
		const int32 UseZ = Compiler->Step(Compiler->Max(NX, NY), NZ);						// floor / ceiling: XY plane
		const int32 UseX = Compiler->Mul(Compiler->Sub(One, UseZ), Compiler->Step(NY, NX));	// walls facing X: YZ plane
		PatternU = Compiler->Lerp(Compiler->Lerp(Compiler->Constant3(1, 0, 0), Compiler->Constant3(0, 1, 0), UseX), Compiler->Constant3(1, 0, 0), UseZ);
		PatternV = Compiler->Lerp(Compiler->Constant3(0, 0, 1), Compiler->Constant3(0, 1, 0), UseZ);
	}
	else
	{
		ScreenPx = Compiler->Div(ViewportUV(Compiler, UV), PixelAt1080(Compiler));
	}

	// 2. A grid rotated by AngleDegrees with Spacing between cells, plus its anti-aliasing width
	auto MakeGrid = [&](float AngleDegrees, int32 Spacing) -> FGrid
	{
		const float Radians = FMath::DegreesToRadians(AngleDegrees);
		const float C = FMath::Cos(Radians), S = FMath::Sin(Radians);
		const int32 InvSpacing = Compiler->Div(One, Compiler->Max(Spacing, Compiler->Constant(0.01f)));

		int32 Coord, DerivCoord;
		if (Space == EVrrComicSpace::World)
		{
			// 2a. World: rotate the plane's axes (AxisX = U*c - V*s, AxisY = U*s + V*c), scale to cells, project onto them
			const int32 AxisX = Compiler->Mul(Compiler->Sub(Compiler->Mul(PatternU, Compiler->Constant(C)), Compiler->Mul(PatternV, Compiler->Constant(S))), InvSpacing);
			const int32 AxisY = Compiler->Mul(Compiler->Add(Compiler->Mul(PatternU, Compiler->Constant(S)), Compiler->Mul(PatternV, Compiler->Constant(C))), InvSpacing);
			Coord = Compiler->AppendVector(Compiler->Dot(WorldPos, AxisX), Compiler->Dot(WorldPos, AxisY));
			DerivCoord = Compiler->AppendVector(Compiler->Dot(CameraRelativePos, AxisX), Compiler->Dot(CameraRelativePos, AxisY));
		}
		else
		{
			// 2b. Screen: rotate the pixel position, scale to cells
			const int32 PX = X(Compiler, ScreenPx), PY = Y(Compiler, ScreenPx);
			const int32 RX = Compiler->Sub(Compiler->Mul(PX, Compiler->Constant(C)), Compiler->Mul(PY, Compiler->Constant(S)));
			const int32 RY = Compiler->Add(Compiler->Mul(PX, Compiler->Constant(S)), Compiler->Mul(PY, Compiler->Constant(C)));
			Coord = Compiler->Mul(Compiler->AppendVector(RX, RY), InvSpacing);
			DerivCoord = Coord;
		}

		// 2c. Cells per screen pixel. Clamped: at depth edges the derivative spikes and would smear the pattern there.
		const int32 Change = Compiler->Add(Compiler->Abs(Compiler->DDX(DerivCoord)), Compiler->Abs(Compiler->DDY(DerivCoord)));
		const int32 Width = Compiler->Clamp(Compiler->Max(X(Compiler, Change), Y(Compiler, Change)), Compiler->Constant(0.001f), Half);
		return { Coord, Width };
	};

	// Anti-aliased "inside" test: 1 well inside Radius, 0 outside, a one-pixel ramp across the edge
	auto Coverage = [&](int32 Radius, int32 Distance, int32 Width)
	{
		return Compiler->Saturate(Compiler->Add(Compiler->Div(Compiler->Sub(Radius, Distance), Width), Half));
	};
	// Switch a pattern fully on just past its threshold (Amount is 0 at the threshold)
	auto Gate = [&](int32 Amount)
	{
		return Compiler->Saturate(Compiler->Mul(Amount, Compiler->Constant(GateSharpness)));
	};

	const int32 ToneArg = IsConnected(Tone) ? Compiler->ValidCast(Tone.Compile(Compiler), MCT_Float1) : Compiler->Saturate(Luminance(Compiler, ColorArg));

	// 3. Hatching: line half-width grows with how far Tone is under Shadow Level
	int32 Hatch = Zero;
	{
		const int32 Level = Compiler->Max(InputOr(Compiler, ShadowLevelIn, ShadowLevel), Compiler->Constant(0.001f));
		const int32 Dark = Compiler->Saturate(Compiler->Div(Compiler->Sub(Level, ToneArg), Level));	// 0 at the level, 1 at black
		const int32 Spacing = InputOr(Compiler, HatchSpacingIn, HatchSpacing);

		// One set of parallel lines: distance to the nearest line's center vs. the half-width this darkness asks for
		auto Lines = [&](float AngleDegrees, int32 Amount)
		{
			const FGrid Grid = MakeGrid(AngleDegrees, Spacing);
			const int32 Distance = Compiler->Abs(Compiler->Sub(Compiler->Frac(X(Compiler, Grid.Coord)), Half));
			const int32 HalfWidth = Compiler->Mul(Amount, Compiler->Constant(HatchMaxWidth * 0.5f));
			return Compiler->Mul(Coverage(HalfWidth, Distance, Grid.Width), Gate(Dark));
		};

		// 3a. First set
		Hatch = Lines(HatchAngle, Dark);

		// 3b. Second set, 60 degrees off the first, over the darkest part of the range
		if (bCrossHatch)
		{
			const float Start = FMath::Clamp(CrossHatchStart, 0.01f, 1.0f);
			const int32 Darker = Compiler->Saturate(Compiler->Div(Compiler->Sub(Dark, Compiler->Constant(1.0f - Start)), Compiler->Constant(Start)));
			Hatch = Compiler->Max(Hatch, Compiler->Mul(Lines(HatchAngle - 60.0f, Darker), Gate(Darker)));
		}
	}

	// 4. Halftone
	int32 Dots = Zero;
	int32 DotArea = Zero;	// Hi-Fi Rush: the fraction of each cell the dots cover, so the gaps can balance them
	const FGrid DotGrid = MakeGrid(DotAngle, InputOr(Compiler, DotSpacingIn, DotSpacing));
	const int32 DotDistance = Compiler->Length(Compiler->Sub(Compiler->Frac(DotGrid.Coord), Compiler->Constant2(0.5f, 0.5f)));	// to the cell center
	if (DotStyle == EVrrHalftoneStyle::LitFalloff)
	{
		// 4a. Hi-Fi Rush: dot size follows how far Tone is between Shadow Level (no dots) and Highlight Level (merged)
		const int32 Low = InputOr(Compiler, ShadowLevelIn, ShadowLevel);
		const int32 High = InputOr(Compiler, HighlightLevelIn, HighlightLevel);
		const int32 Bright = Compiler->Saturate(Compiler->Div(Compiler->Sub(ToneArg, Low), Compiler->Max(Compiler->Sub(High, Low), Compiler->Constant(0.001f))));

		// Radius up to just past half the cell diagonal (0.707), so the dots fully merge at Highlight Level.
		// sqrt: dot AREA follows brightness, like print.
		const int32 Radius = Compiler->Mul(Compiler->SquareRoot(Bright), Compiler->Constant(0.72f));
		Dots = Compiler->Mul(Coverage(Radius, DotDistance, DotGrid.Width), Gate(Bright));

		// Covered area = circle area in a unit cell, minus the four overlaps with the neighbor cells once the radius
		// passes 0.5. Each overlap is a circular segment: r^2 * acos(0.5 / r) - 0.5 * sqrt(r^2 - 0.25).
		const int32 R2 = Compiler->Mul(Radius, Radius);
		const int32 RO = Compiler->Max(Radius, Compiler->Constant(0.5001f));
		const int32 Overlap = Compiler->Sub(
			Compiler->Mul(Compiler->Mul(RO, RO), Compiler->Arccosine(Compiler->Div(Half, RO))),
			Compiler->Mul(Half, Compiler->SquareRoot(Compiler->Sub(Compiler->Mul(RO, RO), Compiler->Constant(0.25f)))));
		const int32 Overlaps = Compiler->Mul(Compiler->Mul(Overlap, Compiler->Constant(4.0f)), Compiler->Step(Half, Radius));
		DotArea = Compiler->Mul(Compiler->Saturate(Compiler->Sub(Compiler->Mul(R2, Compiler->Constant(PI)), Overlaps)), Gate(Bright));
	}
	else if (DotStyle == EVrrHalftoneStyle::Print)
	{
		// 4b. Print: ink coverage grows with how far Tone is under Highlight Level
		const int32 Level = Compiler->Max(InputOr(Compiler, HighlightLevelIn, HighlightLevel), Compiler->Constant(0.001f));
		const int32 Dark = Compiler->Saturate(Compiler->Div(Compiler->Sub(Level, ToneArg), Level));

		// Euclidean spot (the classic print dot): 1 at dot centers, 0 halfway between them. Ink where it's over a
		// threshold that falls with darkness: round dots, then a checkerboard at mid-gray, then round paper holes.
		const int32 Cell = Compiler->Mul(Compiler->Frac(DotGrid.Coord), Compiler->Constant(2.0f * PI));
		const int32 CU = X(Compiler, Cell), CV = Y(Compiler, Cell);
		const int32 Spot = Compiler->Add(Compiler->Mul(Compiler->Add(Compiler->Cosine(CU), Compiler->Cosine(CV)), Compiler->Constant(0.25f)), Half);

		// Slightly past 0-1 so paper is fully clean and black is fully solid
		const int32 Threshold = Compiler->Sub(Compiler->Constant(1.02f), Compiler->Mul(Dark, Compiler->Constant(1.04f)));

		// Anti-aliasing: spot change per pixel, from its slope (kept above a floor where the slope is flat, at dot centers)
		const int32 Slope = Compiler->Mul(Compiler->Add(Compiler->Abs(Compiler->Sine(CU)), Compiler->Abs(Compiler->Sine(CV))), Compiler->Mul(DotGrid.Width, Compiler->Constant(0.5f * PI)));
		const int32 Width = Compiler->Max(Slope, Compiler->Mul(DotGrid.Width, Compiler->Constant(0.25f)));
		Dots = Compiler->Saturate(Compiler->Add(Compiler->Div(Compiler->Sub(Spot, Threshold), Width), Half));
	}
	else
	{
		// 4c. Highlights: dot area grows with how far Tone is over Highlight Level (sqrt: area follows brightness)
		const int32 Level = InputOr(Compiler, HighlightLevelIn, HighlightLevel);
		const int32 Bright = Compiler->Saturate(Compiler->Div(Compiler->Sub(ToneArg, Level), Compiler->Max(Compiler->Sub(One, Level), Compiler->Constant(0.001f))));
		const int32 Radius = Compiler->Mul(Compiler->SquareRoot(Bright), Compiler->Constant(DotMaxSize * 0.5f));
		Dots = Compiler->Mul(Coverage(Radius, DotDistance, DotGrid.Width), Gate(Bright));
	}

	// 5. Distance fade from this pixel's depth, so the sky never gets a pattern
	{
		const int32 Depth = X(Compiler, Compiler->SceneTextureLookup(Compiler->GetViewportUV(), PPI_SceneDepth, false, false, false));
		const float Range = FMath::Max(FadeEnd - FadeStart, 1.0f);
		const int32 Fade = Compiler->Sub(One, Compiler->Saturate(Compiler->Div(Compiler->Sub(Depth, Compiler->Constant(FadeStart)), Compiler->Constant(Range))));
		Hatch = Compiler->Mul(Hatch, Fade);
		Dots = Compiler->Mul(Dots, Fade);
	}

	if (OutputIndex == VrrComicShade::Hatch)
	{
		return Hatch;
	}
	if (OutputIndex == VrrComicShade::Dots)
	{
		return Dots;
	}

	// 6. Composite
	// 6a. Lines multiply toward Ink Color
	const int32 Ink = ColorOr(Compiler, InkColorIn, InkColor);
	int32 Result = Compiler->Lerp(ColorArg, Compiler->Mul(ColorArg, Ink), Compiler->Mul(Hatch, InputOr(Compiler, HatchOpacityIn, HatchOpacity)));
	const int32 Opacity = InputOr(Compiler, DotOpacityIn, DotOpacity);

	// 6b. Hi-Fi Rush: dots brighten and gaps darken. 1 + Opacity * (Dots - Area) averages to 1 over a cell, so the
	//     overall brightness doesn't change. Dots are tinted by Dot Color.
	if (DotStyle == EVrrHalftoneStyle::LitFalloff)
	{
		Result = Compiler->Mul(Result, Compiler->Add(One, Compiler->Mul(Opacity, Compiler->Sub(Dots, DotArea))));
		return Compiler->Lerp(Result, Compiler->Mul(Result, ColorOr(Compiler, DotColorIn, DotColor)), Dots);
	}

	// 6c. Other styles: dots blend toward Dot Color, or multiply toward Ink Color for Print
	const int32 DotTarget = DotStyle == EVrrHalftoneStyle::Print ? Compiler->Mul(Result, Ink) : ColorOr(Compiler, DotColorIn, DotColor);
	return Compiler->Lerp(Result, DotTarget, Compiler->Mul(Dots, Opacity));
}
#endif
