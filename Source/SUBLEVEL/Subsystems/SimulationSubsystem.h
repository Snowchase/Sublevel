#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SubLevelTypes.h"
#include "SimulationSubsystem.generated.h"

class UEventBusSubsystem;
class AParkingFloor;
class AVehicleAgent;
class AStaffAgent;

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

    // Sim runs only in real game worlds — never the editor world.
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
    {
        return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
    }

    // ── Accessors ────────────────────────────────────────────────
    uint64   GetCurrentTick()  const { return CurrentTick; }
    int32    GetCurrentDay()   const { return CurrentDay; }
    int32    GetHourOfDay()    const { return (int32)((CurrentTick % SubLevelSim::TicksPerDay) / SubLevelSim::TicksPerHour); }
    FRandomStream& GetRNG()          { return SimRNG; }

    // ── Vehicle Registry ─────────────────────────────────────────
    void RegisterVehicle(uint32 VehicleID, FVehicleData& Data, AVehicleAgent* Actor);
    void UnregisterVehicle(uint32 VehicleID);
    FVehicleData*  GetVehicle(uint32 VehicleID);
    AVehicleAgent* GetVehicleActor(uint32 VehicleID);
    int32 GetVehicleCount() const { return VehicleActors.Num(); }
    const TMap<uint32, AVehicleAgent*>& GetVehicleActors() const { return VehicleActors; }

    int32 GetVehicleCountOnTile(int32 FloorIndex, int32 TileID) const;

    // ── Vehicle Spawning (§7 — called by UCitySubsystem) ─────────
    // Spawns at FloorIndex/TileID; SpawnVehicleFromCity uses floor 0's entry gate.
    AVehicleAgent* SpawnVehicleAt(EVehicleType Type, int32 FloorIndex, int32 TileID);
    AVehicleAgent* SpawnVehicleFromCity(EVehicleType Type);

    // ── Staff Registry (§6) ──────────────────────────────────────
    AStaffAgent* HireStaff(EStaffRole Role, int32 FloorIndex);
    AStaffAgent* SpawnStaffActor(const FStaffData& Data);   // used by save/load restore
    const TMap<uint32, AStaffAgent*>& GetStaffActors() const { return StaffActors; }
    int32 GetStaffCount() const { return StaffActors.Num(); }

    // ── Incident Access ──────────────────────────────────────────
    void ScheduleIncident(EIncidentType Type, int32 FloorIndex, int32 TileID, float Severity);
    void ResolveIncident(uint32 IncidentID, int32 BranchIndex);   // player decision path
    void ResolveIncidentByStaff(uint32 IncidentID);               // auto-resolve path (no decision broadcast)
    FIncidentData* FindIncident(uint32 IncidentID) { return Incidents.Find(IncidentID); }
    const TMap<uint32, FIncidentData>& GetIncidents() const { return Incidents; }
    uint32 GetFirstPendingDecisionID() const;                     // 0 = none

    // ── Floor Registration ───────────────────────────────────────
    void RegisterFloor(int32 FloorIndex, AParkingFloor* Floor);
    AParkingFloor* GetFloor(int32 FloorIndex) const;

    // ── RNG Seed (set by GameMode on session start) ───────────────
    void InitializeRNG(int32 Seed);
    int32 GetCurrentSeed() const { return ActiveSeed; }

    // ── Save/Load support (§14) ──────────────────────────────────
    void SetClock(uint64 Tick, int32 Day);
    void ClearAllAgents();   // destroys all vehicle + staff actors, clears incidents

private:

    // ── SimTick ──────────────────────────────────────────────────
    FTimerHandle SimTickHandle;
    static constexpr float SIM_TICK_INTERVAL = SubLevelSim::TickInterval;  // 50ms = 20Hz

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
    TMap<uint32, AStaffAgent*>    StaffActors;   // StaffID -> actor ptr
    TMap<uint32, FIncidentData>   Incidents;     // IncidentID -> data
    TMap<int32,  AParkingFloor*>  Floors;        // FloorIndex -> actor ptr
    uint32 NextVehicleID  = 1;
    uint32 NextIncidentID = 1;
    uint32 NextStaffID    = 1;

    // ── Incident Scheduler ───────────────────────────────────────
    TArray<FPendingEvent> EventQueue;    // Maintained as min-heap
    bool bIncidentScheduleSeeded = false;

    void SeedIncidentSchedule();         // one first event per definition
    void FirePendingEvents();
    void ScheduleNextEvent(EIncidentType Type, int32 FloorIndex);
    float SamplePoissonInterval(float Lambda);  // -ln(rand) / lambda

    // Picks a random unblocked Lane tile on the floor; -1 if none.
    int32 PickIncidentTile(AParkingFloor* Floor);

    // Applies/clears tile blockage + crack bookkeeping for an incident.
    void SetIncidentTileBlocked(FIncidentData& Incident, bool bBlocked);

    // §6 — assigns idle matching-role staff to auto-resolvable incidents.
    void DispatchStaffToIncidents();

    // ── Helpers ──────────────────────────────────────────────────
    UEventBusSubsystem* GetEventBus() const;
};
