#include "MF_ComicShade.h"

#include "MaterialCompiler.h"
#include "VrrMaterialMath.h"

#define LOCTEXT_NAMESPACE "VrrMaterialExpressions"

UMaterialExpressionVrrComicShade::UMaterialExpressionVrrComicShade(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(LOCTEXT("VRR", "VRR"));

	bShowOutputNameOnPin = true;
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Color")));
	Outputs.Add(FExpressionOutput(TEXT("Hatch")));
	Outputs.Add(FExpressionOutput(TEXT("Dots")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionVrrComicShade::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	using namespace VrrMaterialMath;

	if (!Color.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Vrr Comic Shade: missing Color input"));
	}
	const int32 Zero = Compiler->Constant(0.0f);
	const int32 One = Compiler->Constant(1.0f);
	const int32 Half = Compiler->Constant(0.5f);
	const int32 ColorArg = Compiler->ValidCast(Color.Compile(Compiler), MCT_Float3);
	auto ColorOr = [&](FExpressionInput& Input, const FLinearColor& Default)
	{
		return Input.GetTracedInput().Expression ? Compiler->ValidCast(Input.Compile(Compiler), MCT_Float3) : Compiler->Constant3(Default.R, Default.G, Default.B);
	};

	// Pattern space. Each pattern needs a 2D grid coordinate (in cells) for the shape, and the same coordinate built
	// from plain floats for screen derivatives (world positions are large-world types, which have no derivatives).
	//   Screen: 1080p pixels.  World: world position projected on the plane of the surface's main axis.
	int32 PatternU = INDEX_NONE, PatternV = INDEX_NONE;   // projection axes (World) as float3, or INDEX_NONE (Screen)
	int32 ScreenPx = INDEX_NONE;
	int32 WorldPos = INDEX_NONE, CameraRelativePos = INDEX_NONE;
	if (Space == EVrrComicSpace::World)
	{
		WorldPos = Compiler->WorldPosition(WPT_Default);
		CameraRelativePos = Compiler->WorldPosition(WPT_CameraRelative);
		// Normal from depth: Unlit toon materials don't write usable G-buffer normals
		const int32 Normal = Compiler->Abs(Compiler->Cross(Compiler->DDX(CameraRelativePos), Compiler->DDY(CameraRelativePos)));
		const int32 NX = X(Compiler, Normal), NY = Y(Compiler, Normal), NZ = Z(Compiler, Normal);
		const int32 UseZ = Compiler->Step(Compiler->Max(NX, NY), NZ);                     // floor / ceiling: XY plane
		const int32 UseX = Compiler->Mul(Compiler->Sub(One, UseZ), Compiler->Step(NY, NX)); // walls facing X: YZ plane
		PatternU = Compiler->Lerp(Compiler->Lerp(Compiler->Constant3(1, 0, 0), Compiler->Constant3(0, 1, 0), UseX), Compiler->Constant3(1, 0, 0), UseZ);
		PatternV = Compiler->Lerp(Compiler->Constant3(0, 0, 1), Compiler->Constant3(0, 1, 0), UseZ);
	}
	else
	{
		ScreenPx = Compiler->Div(ViewportUV(Compiler, UV), PixelAt1080(Compiler));
	}

	struct FGrid
	{
		int32 Coord;  // float2 in cells: frac() gives the position inside a cell
		int32 Width;  // cells per screen pixel: the anti-aliasing width
	};
	auto MakeGrid = [&](float AngleDegrees, int32 Spacing) -> FGrid
	{
		const float Radians = FMath::DegreesToRadians(AngleDegrees);
		const float C = FMath::Cos(Radians), S = FMath::Sin(Radians);
		const int32 InvSpacing = Compiler->Div(One, Compiler->Max(Spacing, Compiler->Constant(0.01f)));
		int32 Coord, DerivCoord;
		if (Space == EVrrComicSpace::World)
		{
			// Rotated in-plane axes: AxisX = U*c - V*s, AxisY = U*s + V*c, scaled to cells
			const int32 AxisX = Compiler->Mul(Compiler->Sub(Compiler->Mul(PatternU, Compiler->Constant(C)), Compiler->Mul(PatternV, Compiler->Constant(S))), InvSpacing);
			const int32 AxisY = Compiler->Mul(Compiler->Add(Compiler->Mul(PatternU, Compiler->Constant(S)), Compiler->Mul(PatternV, Compiler->Constant(C))), InvSpacing);
			Coord = Compiler->AppendVector(Compiler->Dot(WorldPos, AxisX), Compiler->Dot(WorldPos, AxisY));
			DerivCoord = Compiler->AppendVector(Compiler->Dot(CameraRelativePos, AxisX), Compiler->Dot(CameraRelativePos, AxisY));
		}
		else
		{
			const int32 PX = X(Compiler, ScreenPx), PY = Y(Compiler, ScreenPx);
			const int32 RX = Compiler->Sub(Compiler->Mul(PX, Compiler->Constant(C)), Compiler->Mul(PY, Compiler->Constant(S)));
			const int32 RY = Compiler->Add(Compiler->Mul(PX, Compiler->Constant(S)), Compiler->Mul(PY, Compiler->Constant(C)));
			Coord = Compiler->Mul(Compiler->AppendVector(RX, RY), InvSpacing);
			DerivCoord = Coord;
		}
		const int32 Change = Compiler->Add(Compiler->Abs(Compiler->DDX(DerivCoord)), Compiler->Abs(Compiler->DDY(DerivCoord)));
		// Clamped: at depth edges the derivative spikes, and a huge width would smear the pattern there
		const int32 Width = Compiler->Clamp(Compiler->Max(X(Compiler, Change), Y(Compiler, Change)), Compiler->Constant(0.001f), Half);
		return { Coord, Width };
	};

	// Anti-aliased "inside" test: 1 well inside Radius, 0 outside, a one-pixel ramp across the edge
	auto Coverage = [&](int32 Radius, int32 Distance, int32 Width)
	{
		return Compiler->Saturate(Compiler->Add(Compiler->Div(Compiler->Sub(Radius, Distance), Width), Half));
	};

	const int32 ToneArg = Tone.GetTracedInput().Expression ? Compiler->ValidCast(Tone.Compile(Compiler), MCT_Float1) : Compiler->Saturate(Luminance(Compiler, ColorArg));

	// 1. Hatching: line half-width grows with how far Tone is under Shadow Level
	int32 Hatch = Zero;
	{
		const int32 Level = Compiler->Max(InputOr(Compiler, ShadowLevelIn, ShadowLevel), Compiler->Constant(0.001f));
		const int32 Dark = Compiler->Saturate(Compiler->Div(Compiler->Sub(Level, ToneArg), Level));
		const int32 Spacing = InputOr(Compiler, HatchSpacingIn, HatchSpacing);
		// Fully off right at the threshold (no hairline from the anti-aliasing ramp)
		const int32 Gate = Compiler->Saturate(Compiler->Mul(Dark, Compiler->Constant(20.0f)));

		auto Lines = [&](float AngleDegrees, int32 Amount)
		{
			const FGrid Grid = MakeGrid(AngleDegrees, Spacing);
			const int32 Distance = Compiler->Abs(Compiler->Sub(Compiler->Frac(X(Compiler, Grid.Coord)), Half));  // to the line's center
			const int32 HalfWidth = Compiler->Mul(Amount, Compiler->Constant(HatchMaxWidth * 0.5f));
			return Compiler->Mul(Coverage(HalfWidth, Distance, Grid.Width), Gate);
		};
		Hatch = Lines(HatchAngle, Dark);
		if (bCrossHatch)
		{
			// Second set, 60 degrees off the first, over the darkest part of the range
			const float Start = FMath::Clamp(CrossHatchStart, 0.01f, 1.0f);
			const int32 Darker = Compiler->Saturate(Compiler->Div(Compiler->Sub(Dark, Compiler->Constant(1.0f - Start)), Compiler->Constant(Start)));
			Hatch = Compiler->Max(Hatch, Compiler->Mul(Lines(HatchAngle - 60.0f, Darker), Compiler->Saturate(Compiler->Mul(Darker, Compiler->Constant(20.0f)))));
		}
	}

	// 2. Halftone
	int32 Dots = Zero;
	int32 DotArea = Zero;  // Hi-Fi Rush: the fraction of each cell the dots cover, so the gaps can balance them
	if (DotStyle == EVrrHalftoneStyle::LitFalloff)
	{
		// Dot size follows how far Tone is between Shadow Level (no dots) and Highlight Level (merged)
		const int32 Low = InputOr(Compiler, ShadowLevelIn, ShadowLevel);
		const int32 High = InputOr(Compiler, HighlightLevelIn, HighlightLevel);
		const int32 Bright = Compiler->Saturate(Compiler->Div(Compiler->Sub(ToneArg, Low), Compiler->Max(Compiler->Sub(High, Low), Compiler->Constant(0.001f))));
		const FGrid Grid = MakeGrid(DotAngle, InputOr(Compiler, DotSpacingIn, DotSpacing));
		const int32 Distance = Compiler->Length(Compiler->Sub(Compiler->Frac(Grid.Coord), Compiler->Constant2(0.5f, 0.5f)));
		// Up to just past half the cell diagonal, so the dots fully merge at Highlight Level
		const int32 Radius = Compiler->Mul(Compiler->SquareRoot(Bright), Compiler->Constant(0.72f));
		const int32 Gate = Compiler->Saturate(Compiler->Mul(Bright, Compiler->Constant(20.0f)));
		Dots = Compiler->Mul(Coverage(Radius, Distance, Grid.Width), Gate);
		// Circle area in a unit cell, minus the four overlaps with the neighbor cells once the radius passes 0.5
		const int32 R2 = Compiler->Mul(Radius, Radius);
		const int32 RO = Compiler->Max(Radius, Compiler->Constant(0.5001f));
		const int32 Overlap = Compiler->Sub(
			Compiler->Mul(Compiler->Mul(RO, RO), Compiler->Arccosine(Compiler->Div(Half, RO))),
			Compiler->Mul(Half, Compiler->SquareRoot(Compiler->Sub(Compiler->Mul(RO, RO), Compiler->Constant(0.25f)))));
		const int32 Overlaps = Compiler->Mul(Compiler->Mul(Overlap, Compiler->Constant(4.0f)), Compiler->Step(Half, Radius));
		DotArea = Compiler->Mul(Compiler->Saturate(Compiler->Sub(Compiler->Mul(R2, Compiler->Constant(PI)), Overlaps)), Gate);
	}
	else if (DotStyle == EVrrHalftoneStyle::Print)
	{
		// Ink coverage grows with how far Tone is under Highlight Level
		const int32 Level = Compiler->Max(InputOr(Compiler, HighlightLevelIn, HighlightLevel), Compiler->Constant(0.001f));
		const int32 Dark = Compiler->Saturate(Compiler->Div(Compiler->Sub(Level, ToneArg), Level));
		const FGrid Grid = MakeGrid(DotAngle, InputOr(Compiler, DotSpacingIn, DotSpacing));
		// Euclidean spot (the classic print dot): 1 at dot centers, 0 halfway between them. Ink where it's over a
		// threshold that falls with darkness: round dots, then a checkerboard at mid-gray, then round paper holes.
		const int32 Cell = Compiler->Mul(Compiler->Frac(Grid.Coord), Compiler->Constant(2.0f * PI));
		const int32 CU = X(Compiler, Cell), CV = Y(Compiler, Cell);
		const int32 Spot = Compiler->Add(Compiler->Mul(Compiler->Add(Compiler->Cosine(CU), Compiler->Cosine(CV)), Compiler->Constant(0.25f)), Half);
		// Slightly past 0-1 so paper is fully clean and black is fully solid
		const int32 Threshold = Compiler->Sub(Compiler->Constant(1.02f), Compiler->Mul(Dark, Compiler->Constant(1.04f)));
		// Spot change per pixel, from its slope (kept above a floor where the slope is flat, at dot centers)
		const int32 Slope = Compiler->Mul(Compiler->Add(Compiler->Abs(Compiler->Sine(CU)), Compiler->Abs(Compiler->Sine(CV))), Compiler->Mul(Grid.Width, Compiler->Constant(0.5f * PI)));
		const int32 Width = Compiler->Max(Slope, Compiler->Mul(Grid.Width, Compiler->Constant(0.25f)));
		Dots = Compiler->Saturate(Compiler->Add(Compiler->Div(Compiler->Sub(Spot, Threshold), Width), Half));
	}
	else
	{
		// Dot area grows with how far Tone is over Highlight Level
		const int32 Level = InputOr(Compiler, HighlightLevelIn, HighlightLevel);
		const int32 Bright = Compiler->Saturate(Compiler->Div(Compiler->Sub(ToneArg, Level), Compiler->Max(Compiler->Sub(One, Level), Compiler->Constant(0.001f))));
		const FGrid Grid = MakeGrid(DotAngle, InputOr(Compiler, DotSpacingIn, DotSpacing));
		const int32 Distance = Compiler->Length(Compiler->Sub(Compiler->Frac(Grid.Coord), Compiler->Constant2(0.5f, 0.5f)));
		// sqrt: dot AREA follows brightness, like print
		const int32 Radius = Compiler->Mul(Compiler->SquareRoot(Bright), Compiler->Constant(DotMaxSize * 0.5f));
		Dots = Compiler->Mul(Coverage(Radius, Distance, Grid.Width), Compiler->Saturate(Compiler->Mul(Bright, Compiler->Constant(20.0f))));
	}

	// 3. Distance fade (this pixel's depth, so the sky never gets a pattern)
	{
		const int32 Depth = X(Compiler, Compiler->SceneTextureLookup(Compiler->GetViewportUV(), PPI_SceneDepth, false, false, false));
		const float Range = FMath::Max(FadeEnd - FadeStart, 1.0f);
		const int32 Fade = Compiler->Sub(One, Compiler->Saturate(Compiler->Div(Compiler->Sub(Depth, Compiler->Constant(FadeStart)), Compiler->Constant(Range))));
		Hatch = Compiler->Mul(Hatch, Fade);
		Dots = Compiler->Mul(Dots, Fade);
	}

	if (OutputIndex == 1)
	{
		return Hatch;
	}
	if (OutputIndex == 2)
	{
		return Dots;
	}

	// Lines multiply toward Ink Color
	const int32 Ink = ColorOr(InkColorIn, InkColor);
	int32 Result = Compiler->Lerp(ColorArg, Compiler->Mul(ColorArg, Ink), Compiler->Mul(Hatch, InputOr(Compiler, HatchOpacityIn, HatchOpacity)));
	const int32 Opacity = InputOr(Compiler, DotOpacityIn, DotOpacity);
	if (DotStyle == EVrrHalftoneStyle::LitFalloff)
	{
		// Dots brighten, gaps darken: 1 + Opacity * (Dots - Area) averages to 1 over a cell. Dots are tinted by Dot Color.
		Result = Compiler->Mul(Result, Compiler->Add(One, Compiler->Mul(Opacity, Compiler->Sub(Dots, DotArea))));
		return Compiler->Lerp(Result, Compiler->Mul(Result, ColorOr(DotColorIn, DotColor)), Dots);
	}
	// Dots blend toward Dot Color, or multiply toward Ink Color for Print
	const int32 DotTarget = DotStyle == EVrrHalftoneStyle::Print ? Compiler->Mul(Result, Ink) : ColorOr(DotColorIn, DotColor);
	return Compiler->Lerp(Result, DotTarget, Compiler->Mul(Dots, Opacity));
}

void UMaterialExpressionVrrComicShade::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Vrr Comic Shade"));
}
#endif

#undef LOCTEXT_NAMESPACE
