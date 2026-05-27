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
            "EnhancedInput",   // Input context switching (manage vs build mode)
            "UMG",             // All HUD widgets
            "SlateCore",
            "Slate",
            "Json",            // Save file serialization
            "JsonUtilities"    // FJsonObjectConverter helpers
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "SQLiteCore"       // Analytics run history (offline writes only)
        });

        // Ensure UMG is available for widget class declarations in headers
        PublicIncludePaths.AddRange(new string[] { "SUBLEVEL/Public" });
        PrivateIncludePaths.AddRange(new string[] { "SUBLEVEL/Private" });
    }
}
