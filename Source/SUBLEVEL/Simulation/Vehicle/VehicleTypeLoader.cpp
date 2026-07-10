#include "Simulation/Vehicle/VehicleTypeLoader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

TArray<FVehicleTypeDef> FVehicleTypeLoader::LoadedDefinitions;
bool FVehicleTypeLoader::bLoadAttempted = false;

bool FVehicleTypeLoader::LoadFromFile(const FString& FilePath, TArray<FVehicleTypeDef>& OutDefs)
{
    FString JsonRaw;
    if (!FFileHelper::LoadFileToString(JsonRaw, *FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("[VehicleTypeLoader] Could not read %s"), *FilePath);
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonRaw);
    if (!FJsonSerializer::Deserialize(Reader, Root))
    {
        UE_LOG(LogTemp, Error, TEXT("[VehicleTypeLoader] JSON parse failed for %s"), *FilePath);
        return false;
    }

    OutDefs.Reset();

    for (const TSharedPtr<FJsonValue>& Entry : Root)
    {
        const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
        if (!Obj.IsValid()) continue;

        const UEnum* Enum = StaticEnum<EVehicleType>();
        const int64 TypeValue = Enum->GetValueByNameString(Obj->GetStringField(TEXT("type")));
        if (TypeValue == INDEX_NONE)
        {
            UE_LOG(LogTemp, Warning, TEXT("[VehicleTypeLoader] Unknown vehicle type '%s' — skipped"),
                *Obj->GetStringField(TEXT("type")));
            continue;
        }

        FVehicleTypeDef Def;
        Def.Type = (EVehicleType)TypeValue;

        double Number = 0.0;
        if (Obj->TryGetNumberField(TEXT("speed_modifier"), Number))   Def.SpeedModifier   = (float)Number;
        if (Obj->TryGetNumberField(TEXT("spawn_weight"), Number))     Def.SpawnWeight     = (float)Number;
        if (Obj->TryGetNumberField(TEXT("revenue_modifier"), Number)) Def.RevenueModifier = (float)Number;

        int32 Ticks = 0;
        if (Obj->TryGetNumberField(TEXT("park_duration_min_ticks"), Ticks)) Def.ParkDurationMinTicks = Ticks;
        if (Obj->TryGetNumberField(TEXT("park_duration_max_ticks"), Ticks)) Def.ParkDurationMaxTicks = Ticks;

        Obj->TryGetBoolField(TEXT("is_incident_trigger"), Def.bIsIncidentTrigger);

        OutDefs.Add(Def);
    }

    UE_LOG(LogTemp, Log, TEXT("[VehicleTypeLoader] Loaded %d vehicle type definitions"), OutDefs.Num());
    return OutDefs.Num() > 0;
}

void FVehicleTypeLoader::EnsureLoaded()
{
    if (bLoadAttempted) return;
    bLoadAttempted = true;

    const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("VehicleTypes.json"));
    LoadFromFile(Path, LoadedDefinitions);
}

const FVehicleTypeDef* FVehicleTypeLoader::FindDefinition(EVehicleType Type)
{
    EnsureLoaded();
    for (const FVehicleTypeDef& Def : LoadedDefinitions)
    {
        if (Def.Type == Type) return &Def;
    }
    return nullptr;
}

const TArray<FVehicleTypeDef>& FVehicleTypeLoader::GetAllDefinitions()
{
    EnsureLoaded();
    return LoadedDefinitions;
}

EVehicleType FVehicleTypeLoader::PickWeightedRandom(FRandomStream& RNG, float ShadowStanding)
{
    EnsureLoaded();
    if (LoadedDefinitions.Num() == 0) return EVehicleType::Standard;

    float TotalWeight = 0.f;
    for (const FVehicleTypeDef& Def : LoadedDefinitions)
    {
        float Weight = Def.SpawnWeight;
        if (Def.bIsIncidentTrigger && ShadowStanding >= 20.f) Weight *= 3.f;
        TotalWeight += Weight;
    }
    if (TotalWeight <= 0.f) return EVehicleType::Standard;

    float Roll = RNG.FRand() * TotalWeight;
    for (const FVehicleTypeDef& Def : LoadedDefinitions)
    {
        float Weight = Def.SpawnWeight;
        if (Def.bIsIncidentTrigger && ShadowStanding >= 20.f) Weight *= 3.f;
        Roll -= Weight;
        if (Roll <= 0.f) return Def.Type;
    }
    return LoadedDefinitions.Last().Type;
}
