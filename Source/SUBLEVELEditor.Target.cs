using UnrealBuildTool;

public class SUBLEVELEditorTarget : TargetRules
{
    public SUBLEVELEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion  = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("SUBLEVEL");
    }
}
