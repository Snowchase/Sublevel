#include "Persistence/SaveSystem.h"
#include "Subsystems/SimulationSubsystem.h"
#include "Subsystems/EconomySubsystem.h"
#include "Simulation/Vehicle/VehicleAgent.h"
#include "Simulation/FloorGrid/ParkingFloor.h"
#include "Staff/StaffAgent.h"
#include "Framework/SubLevelGameState.h"
#include "SubLevelTypes.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// §14 — Full JSON snapshot of game state.
// Deterministic resume is approximate: the RNG is re-seeded from the
// original session seed, but its stream position is not preserved.

namespace
{
    constexpr int32 SaveVersion = 1;

    FString SaveDirectory()
    {
        return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"));
    }

    FString SlotPath(const FString& SlotName)
    {
        return FPaths::Combine(SaveDirectory(), SlotName + TEXT(".json"));
    }
}

bool FSaveSystem::SaveToFile(const FString& SlotName, const UWorld* World)
{
    if (!World) return false;

    USimulationSubsystem* Sim     = World->GetSubsystem<USimulationSubsystem>();
    UEconomySubsystem*    Economy = World->GetSubsystem<UEconomySubsystem>();
    ASubLevelGameState*   GS      = World->GetGameState<ASubLevelGameState>();
    if (!Sim || !Economy || !GS) return false;

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("version"), SaveVersion);

    // ── Sim clock ────────────────────────────────────────────────
    {
        TSharedRef<FJsonObject> SimObj = MakeShared<FJsonObject>();
        SimObj->SetNumberField(TEXT("seed"), Sim->GetCurrentSeed());
        SimObj->SetNumberField(TEXT("tick"), (double)Sim->GetCurrentTick());
        SimObj->SetNumberField(TEXT("day"),  Sim->GetCurrentDay());
        Root->SetObjectField(TEXT("sim"), SimObj);
    }

    // ── Economy ──────────────────────────────────────────────────
    {
        TSharedRef<FJsonObject> EconObj = MakeShared<FJsonObject>();
        EconObj->SetNumberField(TEXT("balance"),        Economy->GetCurrentBalance());
        EconObj->SetNumberField(TEXT("loan_principal"), Economy->GetLoanPrincipal());
        EconObj->SetNumberField(TEXT("daily_revenue"),  Economy->GetDailyRevenue());
        Root->SetObjectField(TEXT("economy"), EconObj);
    }

    // ── Factions ─────────────────────────────────────────────────
    {
        const FFactionScores& Scores = GS->GetFactionScoresRaw();
        TSharedRef<FJsonObject> FactionObj = MakeShared<FJsonObject>();
        FactionObj->SetNumberField(TEXT("city_authority"), Scores.CityAuthority);
        FactionObj->SetNumberField(TEXT("police"),         Scores.Police);
        FactionObj->SetNumberField(TEXT("shadow_clients"), Scores.ShadowClients);
        Root->SetObjectField(TEXT("factions"), FactionObj);
    }

    // ── Floors ───────────────────────────────────────────────────
    {
        TArray<TSharedPtr<FJsonValue>> FloorArray;
        for (int32 FloorIdx = 0; FloorIdx < 5; ++FloorIdx)
        {
            TSharedRef<FJsonObject> FloorObj = MakeShared<FJsonObject>();
            FloorObj->SetNumberField(TEXT("index"),     FloorIdx);
            FloorObj->SetBoolField(TEXT("unlocked"),    GS->IsFloorUnlocked(FloorIdx));
            FloorObj->SetNumberField(TEXT("integrity"), GS->GetFloorIntegrity(FloorIdx));
            FloorArray.Add(MakeShared<FJsonValueObject>(FloorObj));
        }
        Root->SetArrayField(TEXT("floors"), FloorArray);
    }

    // ── Staff ────────────────────────────────────────────────────
    {
        TArray<TSharedPtr<FJsonValue>> StaffArray;
        for (const auto& [ID, Agent] : Sim->GetStaffActors())
        {
            if (!Agent) continue;
            const FStaffData& Data = Agent->GetStaffData();

            TSharedRef<FJsonObject> StaffObj = MakeShared<FJsonObject>();
            StaffObj->SetNumberField(TEXT("id"),      (double)Data.ID);
            StaffObj->SetNumberField(TEXT("role"),    (double)(uint8)Data.Role);
            StaffObj->SetNumberField(TEXT("trait"),   (double)(uint8)Data.Trait);
            StaffObj->SetNumberField(TEXT("floor"),   Data.AssignedFloor);
            StaffObj->SetNumberField(TEXT("tile"),    Data.CurrentTileID);
            StaffObj->SetNumberField(TEXT("fatigue"), Data.Fatigue);
            StaffObj->SetNumberField(TEXT("loyalty"), Data.Loyalty);
            StaffArray.Add(MakeShared<FJsonValueObject>(StaffObj));
        }
        Root->SetArrayField(TEXT("staff"), StaffArray);
    }

    // ── Vehicles (parked only — vehicles in transit are not restored) ──
    {
        TArray<TSharedPtr<FJsonValue>> VehicleArray;
        for (const auto& [ID, Agent] : Sim->GetVehicleActors())
        {
            if (!Agent) continue;
            const FVehicleData& Data = Agent->GetVehicleData();
            if (Data.State != EVehicleState::Parked) continue;

            TSharedRef<FJsonObject> VehicleObj = MakeShared<FJsonObject>();
            VehicleObj->SetNumberField(TEXT("type"),         (double)(uint8)Data.Type);
            VehicleObj->SetNumberField(TEXT("floor"),        Data.FloorIndex);
            VehicleObj->SetNumberField(TEXT("tile"),         Data.CurrentTileID);
            VehicleObj->SetNumberField(TEXT("park_start"),   (double)Data.ParkStartTick);
            VehicleObj->SetNumberField(TEXT("despawn_tick"), (double)Data.DespawnTick);
            VehicleArray.Add(MakeShared<FJsonValueObject>(VehicleObj));
        }
        Root->SetArrayField(TEXT("vehicles"), VehicleArray);
    }

    // ── Write ────────────────────────────────────────────────────
    FString Output;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    if (!FJsonSerializer::Serialize(Root, Writer)) return false;

    IFileManager::Get().MakeDirectory(*SaveDirectory(), /*Tree =*/ true);
    const bool bOk = FFileHelper::SaveStringToFile(Output, *SlotPath(SlotName));

    UE_LOG(LogTemp, Log, TEXT("[SaveSystem] Save '%s' → %s"), *SlotName,
        bOk ? TEXT("OK") : TEXT("FAILED"));
    return bOk;
}

bool FSaveSystem::LoadFromFile(const FString& SlotName, UWorld* World)
{
    if (!World) return false;

    USimulationSubsystem* Sim     = World->GetSubsystem<USimulationSubsystem>();
    UEconomySubsystem*    Economy = World->GetSubsystem<UEconomySubsystem>();
    ASubLevelGameState*   GS      = World->GetGameState<ASubLevelGameState>();
    if (!Sim || !Economy || !GS) return false;

    FString JsonRaw;
    if (!FFileHelper::LoadFileToString(JsonRaw, *SlotPath(SlotName))) return false;

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonRaw);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;

    // ── Wipe current agents before restoring ─────────────────────
    Sim->ClearAllAgents();

    // ── Sim clock ────────────────────────────────────────────────
    const TSharedPtr<FJsonObject>* SimObj = nullptr;
    if (Root->TryGetObjectField(TEXT("sim"), SimObj))
    {
        int32 Seed = 0;
        double Tick = 0.0;
        int32 Day = 1;
        (*SimObj)->TryGetNumberField(TEXT("seed"), Seed);
        (*SimObj)->TryGetNumberField(TEXT("tick"), Tick);
        (*SimObj)->TryGetNumberField(TEXT("day"),  Day);

        Sim->InitializeRNG(Seed);
        Sim->SetClock((uint64)Tick, Day);
        GS->ActiveSeed = Seed;
    }

    // ── Economy ──────────────────────────────────────────────────
    const TSharedPtr<FJsonObject>* EconObj = nullptr;
    if (Root->TryGetObjectField(TEXT("economy"), EconObj))
    {
        double Balance = 10000.0, Loan = 0.0, Daily = 0.0;
        (*EconObj)->TryGetNumberField(TEXT("balance"),        Balance);
        (*EconObj)->TryGetNumberField(TEXT("loan_principal"), Loan);
        (*EconObj)->TryGetNumberField(TEXT("daily_revenue"),  Daily);
        Economy->SetEconomyState((float)Balance, (float)Loan, (float)Daily);
    }

    // ── Factions ─────────────────────────────────────────────────
    const TSharedPtr<FJsonObject>* FactionObj = nullptr;
    if (Root->TryGetObjectField(TEXT("factions"), FactionObj))
    {
        FFactionScores Scores;
        double Value = 0.0;
        if ((*FactionObj)->TryGetNumberField(TEXT("city_authority"), Value)) Scores.CityAuthority = (float)Value;
        if ((*FactionObj)->TryGetNumberField(TEXT("police"),         Value)) Scores.Police        = (float)Value;
        if ((*FactionObj)->TryGetNumberField(TEXT("shadow_clients"), Value)) Scores.ShadowClients = (float)Value;
        GS->SetFactionScoresRaw(Scores);
    }

    // ── Floors ───────────────────────────────────────────────────
    const TArray<TSharedPtr<FJsonValue>>* FloorArray = nullptr;
    if (Root->TryGetArrayField(TEXT("floors"), FloorArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *FloorArray)
        {
            const TSharedPtr<FJsonObject> FloorObj = Value->AsObject();
            if (!FloorObj.IsValid()) continue;

            int32 Index = 0;
            bool bUnlocked = false;
            double Integrity = 1.0;
            FloorObj->TryGetNumberField(TEXT("index"), Index);
            FloorObj->TryGetBoolField(TEXT("unlocked"), bUnlocked);
            FloorObj->TryGetNumberField(TEXT("integrity"), Integrity);

            GS->SetFloorUnlocked(Index, bUnlocked);
            GS->SetFloorIntegrity(Index, (float)Integrity);
        }
    }

    // ── Staff ────────────────────────────────────────────────────
    const TArray<TSharedPtr<FJsonValue>>* StaffArray = nullptr;
    if (Root->TryGetArrayField(TEXT("staff"), StaffArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *StaffArray)
        {
            const TSharedPtr<FJsonObject> StaffObj = Value->AsObject();
            if (!StaffObj.IsValid()) continue;

            FStaffData Data;
            double Number = 0.0;
            if (StaffObj->TryGetNumberField(TEXT("id"), Number))      Data.ID = (uint32)Number;
            if (StaffObj->TryGetNumberField(TEXT("role"), Number))    Data.Role  = (EStaffRole)(uint8)Number;
            if (StaffObj->TryGetNumberField(TEXT("trait"), Number))   Data.Trait = (EStaffTrait)(uint8)Number;
            if (StaffObj->TryGetNumberField(TEXT("floor"), Number))   Data.AssignedFloor = (int32)Number;
            if (StaffObj->TryGetNumberField(TEXT("tile"), Number))    Data.CurrentTileID = (int32)Number;
            if (StaffObj->TryGetNumberField(TEXT("fatigue"), Number)) Data.Fatigue = (float)Number;
            if (StaffObj->TryGetNumberField(TEXT("loyalty"), Number)) Data.Loyalty = (float)Number;
            Data.Name = FString::Printf(TEXT("Staff %u"), Data.ID);

            Sim->SpawnStaffActor(Data);
        }
    }

    // ── Vehicles ─────────────────────────────────────────────────
    const TArray<TSharedPtr<FJsonValue>>* VehicleArray = nullptr;
    if (Root->TryGetArrayField(TEXT("vehicles"), VehicleArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *VehicleArray)
        {
            const TSharedPtr<FJsonObject> VehicleObj = Value->AsObject();
            if (!VehicleObj.IsValid()) continue;

            double Number = 0.0;
            EVehicleType Type = EVehicleType::Standard;
            int32 Floor = 0, Tile = -1;
            uint64 ParkStart = 0, DespawnTick = 0;

            if (VehicleObj->TryGetNumberField(TEXT("type"), Number))         Type        = (EVehicleType)(uint8)Number;
            if (VehicleObj->TryGetNumberField(TEXT("floor"), Number))        Floor       = (int32)Number;
            if (VehicleObj->TryGetNumberField(TEXT("tile"), Number))         Tile        = (int32)Number;
            if (VehicleObj->TryGetNumberField(TEXT("park_start"), Number))   ParkStart   = (uint64)Number;
            if (VehicleObj->TryGetNumberField(TEXT("despawn_tick"), Number)) DespawnTick = (uint64)Number;

            if (Tile < 0) continue;
            if (AVehicleAgent* Agent = Sim->SpawnVehicleAt(Type, Floor, Tile))
                Agent->RestoreParkedState(ParkStart, DespawnTick);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[SaveSystem] Loaded '%s' — day %d, tick %llu"),
        *SlotName, Sim->GetCurrentDay(), Sim->GetCurrentTick());
    return true;
}

TArray<FString> FSaveSystem::GetSaveSlots()
{
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *FPaths::Combine(SaveDirectory(), TEXT("*.json")),
        /*Files =*/ true, /*Directories =*/ false);

    for (FString& File : Files)
        File = FPaths::GetBaseFilename(File);

    return Files;
}
