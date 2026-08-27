using UnrealBuildTool;

public class UEGT2EditorTarget : TargetRules
{
	public UEGT2EditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new[] { "UEGT2", "UEGT2Editor" });
	}
}
