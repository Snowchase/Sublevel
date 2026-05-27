#include "Simulation/Incident/IncidentLoader.h"

TArray<FIncidentDefinition> FIncidentLoader::LoadedDefinitions;

bool FIncidentLoader::LoadFromFile(const FString& FilePath, TArray<FIncidentDefinition>& OutDefs)
{
    // §5 — parse Data/IncidentTypes.json via UE5 Json module
    // Populate LoadedDefinitions from file
    return false;
}

const FIncidentDefinition* FIncidentLoader::FindDefinition(EIncidentType Type)
{
    for (const FIncidentDefinition& Def : LoadedDefinitions)
    {
        if (Def.Type == Type) return &Def;
    }
    return nullptr;
}
