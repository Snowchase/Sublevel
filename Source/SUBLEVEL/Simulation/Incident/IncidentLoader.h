#pragma once

#include "CoreMinimal.h"
#include "SubLevelTypes.h"

// §5 — Loads Data/IncidentTypes.json at startup.
// Provides incident type definitions and decision branch data.

struct FIncidentBranch
{
    FString        Label;
    FFactionScores FactionDelta;
    FString        ConsequenceTag;
};

struct FIncidentDefinition
{
    EIncidentType           Type              = EIncidentType::FenderBender;
    float                   Lambda            = 0.005f;   // events/second (Poisson rate)
    bool                    bRequiresDecision = false;
    float                   TimerSeconds      = 0.f;      // decision window once visible
    int32                   DefaultBranch     = 0;        // fired on expiry
    EStaffRole              AutoResolveRole   = EStaffRole::Maintenance;
    FFactionScores          ExpireDelta;                  // auto-resolve types: penalty if never handled
    FString                 TileTrigger;
    TArray<FIncidentBranch> Branches;
};

class SUBLEVEL_API FIncidentLoader
{
public:
    static bool LoadFromFile(const FString& FilePath, TArray<FIncidentDefinition>& OutDefs);

    // Lazy-loads Data/IncidentTypes.json on first access. Safe to call every frame.
    static void EnsureLoaded();

    static const FIncidentDefinition* FindDefinition(EIncidentType Type);
    static const TArray<FIncidentDefinition>& GetAllDefinitions();

private:
    static TArray<FIncidentDefinition> LoadedDefinitions;
    static bool bLoadAttempted;
};
