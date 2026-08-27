using UnrealBuildTool;

public class UEGT2Editor : ModuleRules
{
	public UEGT2Editor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UEGT2"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"UnrealEd",
			"Landscape",
			"LandscapeEditor",
			"EditorFramework",
			"EditorSubsystem",
			"AssetTools",
			"AssetRegistry",
			"Foliage",
			"FoliageEdit",
			"Water",
			"WaterEditor",
			"MaterialEditor",
			"RenderCore",
			"RHI",
			"Slate",
			"SlateCore",
			"Projects"
		});
	}
}
