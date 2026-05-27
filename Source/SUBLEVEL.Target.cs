using UnrealBuildTool;

public class SUBLEVELTarget : TargetRules
{
    public SUBLEVELTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V4;
        IncludeOrderVersion  = EngineIncludeOrderVersion.Unreal5_4;
        ExtraModuleNames.Add("SUBLEVEL");
    }
}
