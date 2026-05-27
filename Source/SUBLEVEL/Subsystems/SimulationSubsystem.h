#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SubLevelTypes.h"
#include "SimulationSubsystem.generated.h"

class UEventBusSubsystem;
class AParkingFloor;
class AVehicleAgent;

// ─────────────────────────────────────────────────────────────────
// PENDING EVENT (incident scheduler priority queue entry)
// ─────────────────────────────────────────────────────────────────

USTRUCT()
struct FPendingEvent
{
    GENERATED_BODY()

    uint64          ScheduledTick = 0;
    EIncidentType   Type          = EIncidentType::FenderBender;
    int32           FloorIndex    = 0;
    int32           TileID        = -1;
    float           Severity      = 0.5f;

    // Min-heap comparator — lowest tick fires first
    bool operator<(const FPendingEvent& Other) const
    {
        return ScheduledTick > Other.ScheduledTick;  // Reversed for min-heap
    }
};

// ─────────────────────────────────────────────────────────────────
// SIMULATION SUBSYSTEM
// ─────────────────────────────────────────────────────────────────

UCLASS()
class SUBLEVEL_API USimulationSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // ── Lifecycle ────────────────────────────────────────────────
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ── Accessors ────────────────────────────────────────────────
    uint64   GetCurrentTick()  const { return CurrentTick; }
    int32    GetCurrentDay()   const { return CurrentDay; }
    FRandomStream& GetRNG()          { return SimRNG; }

    // ── Vehicle Registry ─────────────────────────────────────────
    void RegisterVehicle(uint32 VehicleID, FVehicleData& Data, AVehicleAgent* Actor);
    void UnregisterVehicle(uint32 VehicleID);
    FVehicleData*  GetVehicle(uint32 VehicleID);
    AVehicleAgent* GetVehicleActor(uint32 VehicleID);

    int32 GetVehicleCountOnTile(int32 FloorIndex, int32 TileID) const;

    // ── Incident Scheduling ──────────────────────────────────────
    void ScheduleIncident(EIncidentType Type, int32 FloorIndex, int32 TileID, float Severity);
    void ResolveIncident(uint32 IncidentID, int32 BranchIndex);

    // ── Floor Registration ───────────────────────────────────────
    void RegisterFloor(int32 FloorIndex, AParkingFloor* Floor);
    AParkingFloor* GetFloor(int32 FloorIndex) const;

    // ── RNG Seed (set by GameMode on session start) ───────────────
    void InitializeRNG(int32 Seed);
    int32 GetCurrentSeed() const { return ActiveSeed; }

private:

    // ── SimTick ──────────────────────────────────────────────────
    FTimerHandle SimTickHandle;
    static constexpr float SIM_TICK_INTERVAL = 0.05f;  // 50ms = 20Hz

    UFUNCTION()
    void SimTick();

    void TickVehicles();
    void TickIncidentScheduler();
    void TickVisibility();  // Processes light flicker; promotes visible pending incidents
    void TickStaff();
    void TickIntegrity();
    void TickEconomy();     // Delegates to UEconomySubsystem
    void TickCity();        // Delegates to UCitySubsystem

    // ── State ────────────────────────────────────────────────────
    uint64 CurrentTick = 0;
    int32  CurrentDay  = 1;

    // Seeded RNG — all stochastic draws use this
    FRandomStream SimRNG;
    int32         ActiveSeed = 0;

    // ── Registries ───────────────────────────────────────────────
    TMap<uint32, FVehicleData>    Vehicles;      // VehicleID -> sim data
    TMap<uint32, AVehicleAgent*>  VehicleActors; // VehicleID -> actor ptr (not UPROPERTY — managed lifetime)
    TMap<uint32, FIncidentData>   Incidents;     // IncidentID -> data
    TMap<int32,  AParkingFloor*>  Floors;        // FloorIndex -> actor ptr
    uint32 NextVehicleID  = 1;
    uint32 NextIncidentID = 1;

    // ── Incident Scheduler ───────────────────────────────────────
    TArray<FPendingEvent> EventQueue;    // Maintained as min-heap

    void FirePendingEvents();
    void ScheduleNextEvent(EIncidentType Type, int32 FloorIndex);
    float SamplePoissonInterval(float Lambda);  // -ln(rand) / lambda

    // ── Helpers ──────────────────────────────────────────────────
    UEventBusSubsystem* GetEventBus() const;
};
