#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
#include "MF_ComicShade.generated.h"

/** Where the comic patterns are anchored. */
UENUM()
enum class EVrrComicSpace : uint8
{
	/** Fixed to the screen: spacing in pixels at 1080p. Good for a flat, printed-page look. */
	Screen,
	/** Printed on surfaces (planar projection on the surface's main axis): spacing in cm. */
	World,
};

/** What the halftone dots show. */
UENUM()
enum class EVrrHalftoneStyle : uint8
{
	/** Light dots (Dot Color) on bright areas, growing with brightness. Midtones stay clean. */
	Highlights,
	/**
	 * Classic print: ink dots (Ink Color) on everything darker than Highlight Level, growing as it gets darker.
	 * The dots touch at mid-gray, and dark areas become solid ink with shrinking paper-colored holes.
	 */
	Print,
	/**
	 * Hi-Fi RUSH style: dots on lit areas, sized by brightness, so light falloff reads as dots shrinking instead of a
	 * smooth gradient. Dots grow from Shadow Level (set it to Hatch End Level to hand off from the lines) and merge at Highlight Level, so fully lit
	 * areas stay clean. Dots brighten and the gaps darken, keeping the average brightness the same.
	 * Dots shrink with distance and are gone at Dot Shrink Distance.
	 */
	LitFalloff UMETA(DisplayName = "Hi-Fi Rush (Lit Falloff)"),
};

/**
 * Comic print shading for a Post Process material (Hi-Fi RUSH style), applied to Color:
 *
 *  - Hatching: parallel ink lines below Hatch End Level. Lines get thicker the darker the area (up to Hatch Max
 *    Width at black), and a second, crossed set is added in the darkest areas. Over near-black, lines switch to a
 *    light ink so they still show.
 *  - Halftone: round dots on bright areas that grow with brightness, or (Dot Style = Print) classic print ink dots
 *    that grow as areas get darker, or (Dot Style = Hi-Fi Rush) dots that turn light falloff into dot size.
 *  - Midtones stay clean, and the pattern fades out with distance (and on the sky).
 *
 * "Dark" and "bright" come from Tone (0-1). It defaults to Color's luminance. Feed a light mask to Tone to drive the
 * patterns from lighting instead of color (dark-colored lit objects otherwise get hatched).
 * Pattern edges are anti-aliased from screen derivatives, so they stay crisp at any spacing.
 * World space rebuilds the surface normal from depth (Unlit toon materials don't write usable G-buffer normals).
 */
UCLASS(collapsecategories, hidecategories=Object)
class VRR_MATERIALS_API UMaterialExpressionVrrComicShade : public UMaterialExpressionVrrBase
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrComicShade(const FObjectInitializer& ObjectInitializer);

	/** The picture (e.g. Vrr Film Lens). */
	UPROPERTY(meta = (VrrType = "V3", RequiredInput = "true"))
	FExpressionInput Color;

	UPROPERTY(meta = (VrrType = "V2", RequiredInput = "false", ToolTip = "Viewport UV for the screen-space pattern (e.g. Vrr Tape Warp's UV). Defaults to this pixel"))
	FExpressionInput UV;

	UPROPERTY(meta = (VrrType = "S", RequiredInput = "false", ToolTip = "0 = darkest, 1 = brightest. Defaults to Color's luminance"))
	FExpressionInput Tone;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Shadow Level' if not specified"))
	FExpressionInput ShadowLevelIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Highlight Level' if not specified"))
	FExpressionInput HighlightLevelIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Hatch Spacing' if not specified"))
	FExpressionInput HatchSpacingIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Hatch Opacity' if not specified"))
	FExpressionInput HatchOpacityIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Ink Color' if not specified"))
	FExpressionInput InkColorIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Dot Spacing' if not specified"))
	FExpressionInput DotSpacingIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Dot Opacity' if not specified"))
	FExpressionInput DotOpacityIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Dot Color' if not specified"))
	FExpressionInput DotColorIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Dot Shrink Distance' if not specified"))
	FExpressionInput DotShrinkDistanceIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Hatch End Level' if not specified"))
	FExpressionInput HatchEndLevelIn;

	/** Screen: patterns fixed to the screen. World: patterns printed on surfaces. */
	UPROPERTY(EditAnywhere, Category = "Comic Shade")
	EVrrComicSpace Space = EVrrComicSpace::Screen;

	/** Pattern fades out from this distance (cm)... */
	UPROPERTY(EditAnywhere, Category = "Comic Shade", meta = (ClampMin = "0.0"))
	float FadeStart = 4000.0f;

	/** ...and is gone at this distance (cm). The sky is always past it. */
	UPROPERTY(EditAnywhere, Category = "Comic Shade", meta = (ClampMin = "1.0"))
	float FadeEnd = 15000.0f;

	/** Hi-Fi Rush: dots start at this tone and grow toward Highlight Level. (Hatching ends at Hatch End Level.) */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Halftone", meta = (OverridingInputProperty = "ShadowLevelIn", ClampMin = "0.0", ClampMax = "1.0"))
	float ShadowLevel = 0.3f;

	/** Lines are thickest at black (Hatch Max Width), thin out as tone rises, and end at this tone. */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Hatching", meta = (OverridingInputProperty = "HatchEndLevelIn", ClampMin = "0.0", ClampMax = "1.0"))
	float HatchEndLevel = 0.3f;

	/** Distance between lines: pixels at 1080p (Screen) or cm (World). */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Hatching", meta = (OverridingInputProperty = "HatchSpacingIn", ClampMin = "1.0", UIMax = "32.0"))
	float HatchSpacing = 6.0f;

	/** Line width at the darkest tone, as a fraction of the spacing. */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Hatching", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HatchMaxWidth = 0.6f;

	/** Line angle in degrees (0 = vertical lines). */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Hatching", meta = (UIMin = "-90.0", UIMax = "90.0"))
	float HatchAngle = 30.0f;

	/** Add a second set of lines crossing the first in the darkest areas. */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Hatching")
	bool bCrossHatch = true;

	/** Below this fraction of Hatch End Level, the crossing lines start (0.5 = in the darker half of the hatched range). */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Hatching", meta = (EditCondition = "bCrossHatch", ClampMin = "0.0", ClampMax = "1.0"))
	float CrossHatchStart = 0.5f;

	/** How far the line color goes toward Ink Color. */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Hatching", meta = (OverridingInputProperty = "HatchOpacityIn", ClampMin = "0.0", ClampMax = "1.0"))
	float HatchOpacity = 0.5f;

	/** Lines multiply the picture by this color. */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Hatching", meta = (OverridingInputProperty = "InkColorIn"))
	FLinearColor InkColor = FLinearColor(0.05f, 0.04f, 0.12f);

	/**
	 * Where the picture is darker than this (luminance), lines switch to Light Ink Color, so they still show on black
	 * (darkening toward Ink Color can't be seen there). 0 = always dark ink.
	 */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Hatching", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float LightInkBelow = 0.04f;

	/** Line color over near-black areas (see Light Ink Below). */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Hatching")
	FLinearColor LightInkColor = FLinearColor(0.22f, 0.24f, 0.34f);

	/**
	 * Highlights: light dots on bright areas. Print: ink dots that grow as areas get darker (classic print).
	 * Hi-Fi Rush: dots between Shadow Level and Highlight Level that turn light falloff into dot size.
	 */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Halftone")
	EVrrHalftoneStyle DotStyle = EVrrHalftoneStyle::Highlights;

	/**
	 * Highlights: tone above this gets dots (bigger the brighter). Print: tone below this gets ink dots (bigger the
	 * darker). Hi-Fi Rush: the dots have fully merged at this tone.
	 */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Halftone", meta = (OverridingInputProperty = "HighlightLevelIn", ClampMin = "0.0", ClampMax = "1.0"))
	float HighlightLevel = 0.7f;

	/** Distance between dot centers: pixels at 1080p (Screen) or cm (World). */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Halftone", meta = (OverridingInputProperty = "DotSpacingIn", ClampMin = "1.0", UIMax = "32.0"))
	float DotSpacing = 7.0f;

	/** Dot diameter at the brightest tone, as a fraction of the spacing (above 1 the dots merge). Only used by Highlights. */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Halftone", meta = (ClampMin = "0.0", ClampMax = "1.42"))
	float DotMaxSize = 0.9f;

	/** Grid angle in degrees (45 = classic print screen). */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Halftone", meta = (UIMin = "-90.0", UIMax = "90.0"))
	float DotAngle = 45.0f;

	/** How strongly the dots show: toward Dot Color (Highlights), toward Ink Color (Print), or dot/gap contrast (Hi-Fi Rush). */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Halftone", meta = (OverridingInputProperty = "DotOpacityIn", ClampMin = "0.0", ClampMax = "1.0"))
	float DotOpacity = 0.35f;

	/** Highlights dots blend the picture toward this color, and Hi-Fi Rush dots are tinted by it. Print dots use Ink Color. */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Halftone", meta = (OverridingInputProperty = "DotColorIn"))
	FLinearColor DotColor = FLinearColor(1.0f, 1.0f, 1.0f);

	/**
	 * Hi-Fi Rush only: dots are full size at the camera, shrink with distance, and are gone at this distance (cm),
	 * so only nearby objects get the big dots. 0 = no shrinking.
	 */
	UPROPERTY(EditAnywhere, Category = "Comic Shade|Halftone", meta = (OverridingInputProperty = "DotShrinkDistanceIn", ClampMin = "0.0", UIMax = "20000.0"))
	float DotShrinkDistance = 5000.0f;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif
};
