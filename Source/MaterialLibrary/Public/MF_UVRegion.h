// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#pragma once

#include "CoreMinimal.h"
#include "MF_Base.h"
#include "MF_UVRegion.generated.h"

/**
 * Picks one material region out of a TechArt Toolbox "UV Layouts" channel (usually UV1), or tells which region a UV is in.
 *
 * Atlas layouts: every material slot gets a region of the 0-1 square, in material slot order, columns first:
 * 1 region the whole square, 2 two columns, 3 three columns, more fill extra rows (4 = 2 + 2, 5 = 3 + 2, 7 = 3 + 2 + 2).
 * Set Region Count to the number of regions in the layout (the tool's preview shows it) and Region to the one you want.
 *
 * UDIM layouts (UDIM on): region N is the whole tile 1001 + N, ten to a row, later rows one unit down in V:
 * Min = (N mod 10, -floor(N / 10)), Size = 1. Region Count and Max Columns aren't used.
 *
 *   Local UV  = (UV - Min) / Size          0-1 across the chosen region (tile a texture or gradient inside it)
 *   Mask      = 1 inside the chosen region, 0 elsewhere
 *   To Region = Min + UV * Size            places a 0-1 UV into the chosen region (sample one region of a baked atlas)
 *   Bounds    = (Min.U, Min.V, Size.U, Size.V) of the chosen region
 *   Index     = the region the UV itself is in (0, 1, 2 ... in material slot order), whatever Region is set to.
 *               Atlas: UVs outside 0-1 count as the nearest region. UDIM: tile number - 1001.
 *
 * Region and Region Count can be driven by scalar parameters, so material instances can switch regions.
 * UDIM takes a static bool (a Static Bool Parameter), since it changes the shader.
 * The row and column math must stay in step with MakeRegions and MakeUDIMTiles in TechArt Toolbox's Core_UVLayout.cpp.
 */
UCLASS(collapsecategories, hidecategories=Object)
class MATERIALLIBRARY_API UMaterialExpressionVrrUVRegion : public UMaterialExpressionVrrBase
{
	GENERATED_BODY()

public:
	UMaterialExpressionVrrUVRegion(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(meta = (VrrType = "V2", RequiredInput = "false", ToolTip = "UVs to use. Defaults to the 'Coordinate Index' texture coordinate"))
	FExpressionInput Coordinates;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Region' if not specified"))
	FExpressionInput RegionIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'Region Count' if not specified"))
	FExpressionInput RegionCountIn;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Static bool: the layout is UDIM tiles. Defaults to 'UDIM' if not specified"))
	FExpressionInput UDIMIn;

	/** Texture coordinate used when Coordinates isn't connected. The layout tool writes UV1 by default. */
	UPROPERTY(EditAnywhere, Category = "UV Region", meta = (ClampMin = "0", ClampMax = "7"))
	int32 CoordinateIndex = 1;

	/** Which region: 0 is the first material slot in the layout, 1 the next, and so on. */
	UPROPERTY(EditAnywhere, Category = "UV Region", meta = (OverridingInputProperty = "RegionIn", ClampMin = "0"))
	int32 Region = 0;

	/** How many regions the layout has (one per material slot used by the mesh). Not used for UDIM layouts. */
	UPROPERTY(EditAnywhere, Category = "UV Region", meta = (OverridingInputProperty = "RegionCountIn", ClampMin = "1"))
	int32 RegionCount = 1;

	/**
	 * The layout was made with "UDIM tiles (one per material)": region N is the whole tile 1001 + N (ten to a row,
	 * later rows one unit down in V). Region Count and Max Columns are then not used.
	 */
	UPROPERTY(EditAnywhere, Category = "UV Region", meta = (OverridingInputProperty = "UDIMIn", DisplayName = "UDIM"))
	bool bUDIM = false;

	/** Regions per row before the layout starts another row. Must match the layout tool (3). */
	UPROPERTY(EditAnywhere, Category = "UV Region", AdvancedDisplay, meta = (ClampMin = "1"))
	int32 MaxColumns = 3;

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual EMaterialValueType GetInputValueType(int32 InputIndex) override;

private:
	/** UDIM In if connected (it must be static), else the UDIM property. False with an error if it isn't static. */
	bool GetUseUDIM(class FMaterialCompiler* Compiler, bool& bOutUDIM);
	/** The UV the node works on: Coordinates, or the Coordinate Index texture coordinate. */
	int32 CompileUV(class FMaterialCompiler* Compiler);
	/** Which region the UV is in. */
	int32 CompileIndex(class FMaterialCompiler* Compiler, bool bUseUDIM);
	/** Min and Size of the chosen region (Region / Region In). */
	bool CompileRegionBounds(class FMaterialCompiler* Compiler, bool bUseUDIM, int32& OutMin, int32& OutSize);
	/** Atlas rows: Rows, Base (regions in a short row), Extra (rows holding Base + 1) and Big (regions in those rows). */
	void CompileAtlasRows(class FMaterialCompiler* Compiler, int32 Count, int32& OutRows, int32& OutBase, int32& OutExtra, int32& OutBig);
	int32 CompileRegionCount(class FMaterialCompiler* Compiler);
#endif
};
