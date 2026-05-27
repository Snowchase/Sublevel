using UnrealBuildTool;

public class SUBLEVEL : ModuleRules
{
    public SUBLEVEL(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "UMG",
            "SlateCore",
            "Slate",
            "Json",
            "JsonUtilities"
        });
    }
}
