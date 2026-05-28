using UnrealBuildTool;

public class SUBLEVEL : ModuleRules
{
    public SUBLEVEL(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Source/SUBLEVEL/ is the include root so that headers resolve as
        // "SubLevelTypes.h", "Subsystems/SimulationSubsystem.h", etc.
        PublicIncludePaths.Add(ModuleDirectory);

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
