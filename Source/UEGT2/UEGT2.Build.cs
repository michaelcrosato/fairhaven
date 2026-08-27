using UnrealBuildTool;

public class UEGT2 : ModuleRules
{
	public UEGT2(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"PhysicsCore",
			"EnhancedInput",
			"Water"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Slate",
			"SlateCore",
			"ApplicationCore",
			"RenderCore",
			"RHI",
			"AudioMixer",
			"DeveloperSettings",
			"Projects"
		});
	}
}
