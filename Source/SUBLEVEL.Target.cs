using UnrealBuildTool;

public class SUBLEVELTarget : TargetRules
{
    public SUBLEVELTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion  = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("SUBLEVEL");
    }
}
