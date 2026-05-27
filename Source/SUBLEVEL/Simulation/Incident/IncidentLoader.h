#pragma once

#include "CoreMinimal.h"
#include "SubLevelTypes.h"

// §5 — Loads Data/IncidentTypes.json at startup.
// Provides incident type definitions and decision branch data.
// Implemented in a future pass.

struct FIncidentBranch
{
    FString Label;
    FFactionScores FactionDelta;
    FString ConsequenceTag;
};

struct FIncidentDefinition
{
    EIncidentType         Type;
    float                 Lambda          = 0.005f;
    bool                  bRequiresDecision = false;
    float                 TimerSeconds    = 0.f;
    int32                 DefaultBranch   = 0;
    TArray<FIncidentBranch> Branches;
};

class SUBLEVEL_API FIncidentLoader
{
public:
    static bool LoadFromFile(const FString& FilePath, TArray<FIncidentDefinition>& OutDefs);
    static const FIncidentDefinition* FindDefinition(EIncidentType Type);

private:
    static TArray<FIncidentDefinition> LoadedDefinitions;
};
