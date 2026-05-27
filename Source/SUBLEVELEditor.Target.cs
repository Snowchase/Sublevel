using UnrealBuildTool;

public class SUBLEVELEditorTarget : TargetRules
{
    public SUBLEVELEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V4;
        IncludeOrderVersion  = EngineIncludeOrderVersion.Unreal5_4;
        ExtraModuleNames.Add("SUBLEVEL");
    }
}
