using UnrealBuildTool;

public class MaterialLibrary : ModuleRules
{
	public MaterialLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings"	// UVrr_ToonBoilSettings (Project Settings > Plugins > VRR Toon Boil)
		});
	}
}
