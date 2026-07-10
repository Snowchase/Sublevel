#pragma once

#include "CoreMinimal.h"
#include "SubLevelTypes.h"

// §7 — Loads Data/VehicleTypes.json.
// Spawn weights drive city arrival composition; park durations and
// revenue modifiers drive the vehicle FSM and economy.

struct FVehicleTypeDef
{
    EVehicleType Type                 = EVehicleType::Standard;
    float        SpeedModifier        = 1.0f;
    float        SpawnWeight          = 0.0f;
    float        RevenueModifier      = 1.0f;
    int32        ParkDurationMinTicks = 400;
    int32        ParkDurationMaxTicks = 4800;
    bool         bIsIncidentTrigger   = false;
};

class SUBLEVEL_API FVehicleTypeLoader
{
public:
    static bool LoadFromFile(const FString& FilePath, TArray<FVehicleTypeDef>& OutDefs);
    static void EnsureLoaded();

    static const FVehicleTypeDef* FindDefinition(EVehicleType Type);
    static const TArray<FVehicleTypeDef>& GetAllDefinitions();

    // Weighted random draw. ShadowStanding >= 20 triples DeliveryVan weight.
    static EVehicleType PickWeightedRandom(FRandomStream& RNG, float ShadowStanding = 0.f);

private:
    static TArray<FVehicleTypeDef> LoadedDefinitions;
    static bool bLoadAttempted;
};
