#include "Simulation/Incident/IncidentLoader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

TArray<FIncidentDefinition> FIncidentLoader::LoadedDefinitions;
bool FIncidentLoader::bLoadAttempted = false;

namespace
{
    // Parses a {"city_authority": -1, "police": 2, "shadow_clients": 3} object.
    FFactionScores ParseFactionDelta(const TSharedPtr<FJsonObject>& Obj)
    {
        FFactionScores Delta;
        if (!Obj.IsValid()) return Delta;

        double Value = 0.0;
        if (Obj->TryGetNumberField(TEXT("city_authority"), Value)) Delta.CityAuthority = (float)Value;
        if (Obj->TryGetNumberField(TEXT("police"),         Value)) Delta.Police        = (float)Value;
        if (Obj->TryGetNumberField(TEXT("shadow_clients"), Value)) Delta.ShadowClients = (float)Value;
        return Delta;
    }

    bool ParseIncidentType(const FString& Str, EIncidentType& OutType)
    {
        const UEnum* Enum = StaticEnum<EIncidentType>();
        const int64 Value = Enum->GetValueByNameString(Str);
        if (Value == INDEX_NONE) return false;
        OutType = (EIncidentType)Value;
        return true;
    }

    EStaffRole ParseStaffRole(const FString& Str)
    {
        const UEnum* Enum = StaticEnum<EStaffRole>();
        const int64 Value = Enum->GetValueByNameString(Str);
        return (Value == INDEX_NONE) ? EStaffRole::Maintenance : (EStaffRole)Value;
    }
}

bool FIncidentLoader::LoadFromFile(const FString& FilePath, TArray<FIncidentDefinition>& OutDefs)
{
    FString JsonRaw;
    if (!FFileHelper::LoadFileToString(JsonRaw, *FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("[IncidentLoader] Could not read %s"), *FilePath);
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonRaw);
    if (!FJsonSerializer::Deserialize(Reader, Root))
    {
        UE_LOG(LogTemp, Error, TEXT("[IncidentLoader] JSON parse failed for %s"), *FilePath);
        return false;
    }

    OutDefs.Reset();

    for (const TSharedPtr<FJsonValue>& Entry : Root)
    {
        const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
        if (!Obj.IsValid()) continue;

        FIncidentDefinition Def;

        if (!ParseIncidentType(Obj->GetStringField(TEXT("type")), Def.Type))
        {
            UE_LOG(LogTemp, Warning, TEXT("[IncidentLoader] Unknown incident type '%s' — skipped"),
                *Obj->GetStringField(TEXT("type")));
            continue;
        }

        double Number = 0.0;
        if (Obj->TryGetNumberField(TEXT("lambda"), Number))        Def.Lambda       = (float)Number;
        if (Obj->TryGetNumberField(TEXT("timer_seconds"), Number)) Def.TimerSeconds = (float)Number;

        Obj->TryGetBoolField(TEXT("requires_decision"), Def.bRequiresDecision);
        Obj->TryGetStringField(TEXT("tile_trigger"), Def.TileTrigger);

        FString RoleStr;
        if (Obj->TryGetStringField(TEXT("auto_resolve_role"), RoleStr))
            Def.AutoResolveRole = ParseStaffRole(RoleStr);

        const TSharedPtr<FJsonObject>* ExpireDeltaObj = nullptr;
        if (Obj->TryGetObjectField(TEXT("faction_delta_on_expire"), ExpireDeltaObj))
            Def.ExpireDelta = ParseFactionDelta(*ExpireDeltaObj);

        const TArray<TSharedPtr<FJsonValue>>* BranchArray = nullptr;
        if (Obj->TryGetArrayField(TEXT("branches"), BranchArray))
        {
            for (const TSharedPtr<FJsonValue>& BranchValue : *BranchArray)
            {
                const TSharedPtr<FJsonObject> BranchObj = BranchValue->AsObject();
                if (!BranchObj.IsValid()) continue;

                FIncidentBranch Branch;
                BranchObj->TryGetStringField(TEXT("label"), Branch.Label);
                BranchObj->TryGetStringField(TEXT("consequence"), Branch.ConsequenceTag);

                const TSharedPtr<FJsonObject>* DeltaObj = nullptr;
                if (BranchObj->TryGetObjectField(TEXT("faction_delta"), DeltaObj))
                    Branch.FactionDelta = ParseFactionDelta(*DeltaObj);

                Def.Branches.Add(MoveTemp(Branch));
            }
        }

        int32 DefaultBranch = 0;
        if (Obj->TryGetNumberField(TEXT("default_branch"), DefaultBranch))
            Def.DefaultBranch = FMath::Clamp(DefaultBranch, 0, FMath::Max(0, Def.Branches.Num() - 1));

        OutDefs.Add(MoveTemp(Def));
    }

    UE_LOG(LogTemp, Log, TEXT("[IncidentLoader] Loaded %d incident definitions from %s"),
        OutDefs.Num(), *FilePath);
    return OutDefs.Num() > 0;
}

void FIncidentLoader::EnsureLoaded()
{
    if (bLoadAttempted) return;
    bLoadAttempted = true;

    const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("IncidentTypes.json"));
    LoadFromFile(Path, LoadedDefinitions);
}

const FIncidentDefinition* FIncidentLoader::FindDefinition(EIncidentType Type)
{
    EnsureLoaded();
    for (const FIncidentDefinition& Def : LoadedDefinitions)
    {
        if (Def.Type == Type) return &Def;
    }
    return nullptr;
}

const TArray<FIncidentDefinition>& FIncidentLoader::GetAllDefinitions()
{
    EnsureLoaded();
    return LoadedDefinitions;
}
