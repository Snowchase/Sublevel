#include "Subsystems/SimulationSubsystem.h"
#include "Subsystems/EventBusSubsystem.h"
#include "Simulation/FloorGrid/ParkingFloor.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Math/UnrealMathUtility.h"

// ─────────────────────────────────────────────────────────────────
// LIFECYCLE
// ─────────────────────────────────────────────────────────────────

void USimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // EventBus must be initialized before SimulationSubsystem
    Collection.InitializeDependency<UEventBusSubsystem>();

    GetWorld()->GetTimerManager().SetTimer(
        SimTickHandle,
        this,
        &USimulationSubsystem::SimTick,
        SIM_TICK_INTERVAL,
        true    // loop
    );

    UE_LOG(LogTemp, Log, TEXT("[SimulationSubsystem] Initialized. Tick rate: 20Hz"));
}

void USimulationSubsystem::Deinitialize()
{
    GetWorld()->GetTimerManager().ClearTimer(SimTickHandle);
    Vehicles.Empty();
    Incidents.Empty();
    Floors.Empty();
    EventQueue.Empty();

    Super::Deinitialize();
}

// ─────────────────────────────────────────────────────────────────
// RNG
// ─────────────────────────────────────────────────────────────────

void USimulationSubsystem::InitializeRNG(int32 Seed)
{
    ActiveSeed = Seed;
    SimRNG.Initialize(Seed);
    UE_LOG(LogTemp, Log, TEXT("[SimulationSubsystem] RNG seeded: %d"), Seed);
}

// ─────────────────────────────────────────────────────────────────
// SIM TICK — called every 50ms on Game Thread
// ─────────────────────────────────────────────────────────────────

void USimulationSubsystem::SimTick()
{
    ++CurrentTick;

    // Advance day counter (1 in-game day = configurable tick count)
    // 20 ticks/sec * 60 * 24 = 28800 ticks/real-minute if 1:1
    // Tune via time scale multiplier in GameMode — placeholder here
    constexpr uint64 TicksPerDay = 14400;  // ~12 min real-time per day at 20Hz
    if (CurrentTick % TicksPerDay == 0)
    {
        ++CurrentDay;
    }

    // Order matters — city context affects demand before vehicles tick
    TickCity();
    TickIncidentScheduler();
    TickVehicles();
    TickStaff();
    TickIntegrity();
    TickEconomy();
}

// ─────────────────────────────────────────────────────────────────
// TICK PHASES
// ─────────────────────────────────────────────────────────────────

void USimulationSubsystem::TickVehicles()
{
    // Each vehicle advances its FSM one step per SimTick
    // Actual movement interpolation is handled by AVehicleAgent on Engine Tick
    for (auto& [ID, Data] : Vehicles)
    {
        // Stub — VehicleAgent FSM tick will be implemented in §4
        // Vehicle reads its floor's flow field direction, updates CurrentTileID
    }
}

void USimulationSubsystem::TickIncidentScheduler()
{
    FirePendingEvents();

    // Tick active incidents — accumulate severity, check expiry
    for (auto& [ID, Incident] : Incidents)
    {
        if (Incident.State == EIncidentState::Active ||
            Incident.State == EIncidentState::DecisionPending)
        {
            // Severity accumulates while unresolved
            Incident.SeverityAccumulator += Incident.Severity * 0.001f;

            // Decision deadline check
            if (Incident.DecisionDeadlineTick > 0 &&
                CurrentTick >= Incident.DecisionDeadlineTick &&
                Incident.State == EIncidentState::DecisionPending)
            {
                // Fire default (worst-case) branch
                ResolveIncident(ID, 0);  // Branch 0 = default in JSON
                Incident.State = EIncidentState::Expired;
                GetEventBus()->OnIncidentExpired.Broadcast(ID);
            }
        }
    }
}

void USimulationSubsystem::TickStaff()
{
    // Stub — StaffAgent FSM tick, fatigue/loyalty decay
    // Implemented in §6
}

void USimulationSubsystem::TickIntegrity()
{
    // Delegated to each AParkingFloor
    for (auto& [Index, Floor] : Floors)
    {
        if (Floor) Floor->TickIntegrity();
    }
}

void USimulationSubsystem::TickEconomy()
{
    // Delegated to UEconomySubsystem via EventBus revenue events
    // Revenue accumulates per parked vehicle — handled in TickVehicles
}

void USimulationSubsystem::TickCity()
{
    // Delegated to UCitySubsystem
}

// ─────────────────────────────────────────────────────────────────
// INCIDENT SCHEDULING
// ─────────────────────────────────────────────────────────────────

void USimulationSubsystem::FirePendingEvents()
{
    while (EventQueue.Num() > 0 && EventQueue.HeapTop().ScheduledTick <= CurrentTick)
    {
        FPendingEvent E;
        EventQueue.HeapPop(E);

        FIncidentData NewIncident;
        NewIncident.ID          = NextIncidentID++;
        NewIncident.Type        = E.Type;
        NewIncident.State       = EIncidentState::Pending;
        NewIncident.FloorIndex  = E.FloorIndex;
        NewIncident.TileID      = E.TileID;
        NewIncident.Severity    = E.Severity;
        NewIncident.SpawnTick   = CurrentTick;

        Incidents.Add(NewIncident.ID, NewIncident);
        GetEventBus()->OnIncidentSpawned.Broadcast(NewIncident);

        // Schedule next event of this type for this floor (Poisson)
        ScheduleNextEvent(E.Type, E.FloorIndex);
    }
}

void USimulationSubsystem::ScheduleIncident(
    EIncidentType Type, int32 FloorIndex, int32 TileID, float Severity)
{
    FPendingEvent E;
    E.ScheduledTick = CurrentTick + 1;  // Immediate next tick
    E.Type          = Type;
    E.FloorIndex    = FloorIndex;
    E.TileID        = TileID;
    E.Severity      = Severity;

    EventQueue.HeapPush(E);
}

void USimulationSubsystem::ScheduleNextEvent(EIncidentType Type, int32 FloorIndex)
{
    // Lambda values per incident type — tuned via Python balance tools
    // Higher lambda = more frequent
    static const TMap<EIncidentType, float> IncidentLambda =
    {
        { EIncidentType::FenderBender,     0.005f },
        { EIncidentType::OilSpill,         0.003f },
        { EIncidentType::OverstayVehicle,  0.010f },
        { EIncidentType::SuspiciousVehicle,0.002f },
        // ... remaining types
    };

    const float* Lambda = IncidentLambda.Find(Type);
    if (!Lambda) return;

    const float Interval = SamplePoissonInterval(*Lambda);  // In seconds
    const uint64 DelayTicks = FMath::RoundToInt(Interval / SIM_TICK_INTERVAL);

    FPendingEvent Next;
    Next.ScheduledTick = CurrentTick + DelayTicks;
    Next.Type          = Type;
    Next.FloorIndex    = FloorIndex;
    Next.TileID        = -1;  // Assigned at fire time based on floor state
    Next.Severity      = SimRNG.FRandRange(0.3f, 1.0f);

    EventQueue.HeapPush(Next);
}

float USimulationSubsystem::SamplePoissonInterval(float Lambda)
{
    // Inter-arrival time for Poisson process: -ln(U) / lambda
    // U = uniform random (0,1), result in seconds
    const float U = FMath::Max(SimRNG.FRand(), SMALL_NUMBER);  // Avoid ln(0)
    return -FMath::Loge(U) / Lambda;
}

void USimulationSubsystem::ResolveIncident(uint32 IncidentID, int32 BranchIndex)
{
    FIncidentData* Incident = Incidents.Find(IncidentID);
    if (!Incident) return;

    Incident->State = EIncidentState::Resolved;
    GetEventBus()->OnDecisionMade.Broadcast(IncidentID, BranchIndex);
    GetEventBus()->OnIncidentResolved.Broadcast(IncidentID);

    // Faction consequences applied by UEconomySubsystem listening to OnDecisionMade
}

// ─────────────────────────────────────────────────────────────────
// REGISTRIES
// ─────────────────────────────────────────────────────────────────

void USimulationSubsystem::RegisterVehicle(uint32 VehicleID, FVehicleData& Data)
{
    Data.ID = VehicleID;
    Vehicles.Add(VehicleID, Data);
}

void USimulationSubsystem::UnregisterVehicle(uint32 VehicleID)
{
    Vehicles.Remove(VehicleID);
}

FVehicleData* USimulationSubsystem::GetVehicle(uint32 VehicleID)
{
    return Vehicles.Find(VehicleID);
}

void USimulationSubsystem::RegisterFloor(int32 FloorIndex, AParkingFloor* Floor)
{
    Floors.Add(FloorIndex, Floor);
}

AParkingFloor* USimulationSubsystem::GetFloor(int32 FloorIndex) const
{
    AParkingFloor* const* Found = Floors.Find(FloorIndex);
    return Found ? *Found : nullptr;
}

int32 USimulationSubsystem::GetVehicleCountOnTile(int32 FloorIndex, int32 TileID) const
{
    int32 Count = 0;
    for (const auto& [ID, Data] : Vehicles)
    {
        if (Data.FloorIndex == FloorIndex && Data.CurrentTileID == TileID)
            ++Count;
    }
    return Count;
}

// ─────────────────────────────────────────────────────────────────
// HELPERS
// ─────────────────────────────────────────────────────────────────

UEventBusSubsystem* USimulationSubsystem::GetEventBus() const
{
    return UEventBusSubsystem::Get(this);
}
