using UnrealBuildTool;

public class VRR_Materials : ModuleRules
{
	public VRR_Materials(ReadOnlyTargetRules Target) : base(Target)
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
