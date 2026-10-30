// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
#include "MF_WorldChecker.generated.h"

/**
 * Even world-space checker squares on any surface: floors, walls, ceilings and stairs.
 * Projects from the axis the surface faces (XY on floors, YZ or XZ on walls), so every face gets square tiles that
 * stay put in the world, and softens the edges by screen size so distant tiles blend to an even 0.5 instead of
 * shimmering. Output: 0 on one set of squares, 1 on the other; use it as a Lerp alpha between two colors.
 */
UCLASS(collapsecategories, hidecategories=Object)
class MATERIALLIBRARY_API UMaterialExpressionVrrWorldChecker : public UMaterialExpressionVrrBase
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrWorldChecker(const FObjectInitializer& ObjectInitializer);

	/** World position to tile. Defaults to this pixel's. */
	UPROPERTY(meta = (VrrType = "V3", RequiredInput = "false", ToolTip = "World position to tile. Defaults to this pixel's"))
	FExpressionInput WorldPositionIn;

	/** World-space normal that picks the projection plane. Defaults to the mesh normal. */
	UPROPERTY(meta = (VrrType = "V3", RequiredInput = "false", ToolTip = "World-space normal that picks the projection plane. Defaults to the mesh normal"))
	FExpressionInput Normal;

	UPROPERTY(meta = (VrrType = "S", RequiredInput = "false", ToolTip = "Defaults to 'Tile Size' if not specified"))
	FExpressionInput TileSizeIn;

	/** Width of one square in cm. */
	UPROPERTY(EditAnywhere, Category = "Checker", meta = (OverridingInputProperty = "TileSizeIn", ClampMin = "0.01", UIMin = "1.0", UIMax = "1000.0"))
	float TileSize = 100.0f;

	/** Edge blur in screen pixels. 1 = smooth one-pixel edges, 0 = hard edges (shimmers in the distance). */
	UPROPERTY(EditAnywhere, Category = "Checker", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float EdgeSoftness = 1.0f;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif
};
