#include "Subsystems/SimulationSubsystem.h"
#include "Subsystems/EventBusSubsystem.h"
#include "Subsystems/CitySubsystem.h"
#include "Subsystems/EconomySubsystem.h"
#include "Simulation/FloorGrid/ParkingFloor.h"
#include "Simulation/Vehicle/VehicleAgent.h"
#include "Simulation/Vehicle/VehicleTypeLoader.h"
#include "Simulation/Incident/IncidentLoader.h"
#include "Staff/StaffAgent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Math/UnrealMathUtility.h"

// ─────────────────────────────────────────────────────────────────
// LIFECYCLE
// ─────────────────────────────────────────────────────────────────

void USimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

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
    StaffActors.Empty();

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

    SeedIncidentSchedule();
}

// ─────────────────────────────────────────────────────────────────
// SIM TICK — called every 50ms on Game Thread
// ─────────────────────────────────────────────────────────────────

void USimulationSubsystem::SimTick()
{
    ++CurrentTick;

    if (CurrentTick % SubLevelSim::TicksPerDay == 0)
    {
        ++CurrentDay;
    }

    // Order matters — city context affects demand before vehicles tick
    TickCity();
    TickIncidentScheduler();
    TickVisibility();
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
    // Rebuild dirty flow fields on all floors before vehicles move.
    for (auto& [Index, Floor] : Floors)
    {
        if (Floor) Floor->RebuildDirtyFlowFields();
    }

    // Advance each vehicle FSM by one step.
    for (auto& [ID, Agent] : VehicleActors)
    {
        if (Agent && !Agent->IsActorBeingDestroyed())
            Agent->SimTick(CurrentTick);
    }

    // Mirror actor state back into the Vehicles data map.
    for (auto& [ID, Agent] : VehicleActors)
    {
        if (Agent && !Agent->IsActorBeingDestroyed())
        {
            if (FVehicleData* Data = Vehicles.Find(ID))
                *Data = Agent->GetVehicleData();
        }
    }

    // Remove despawned actors from registry.
    for (auto It = VehicleActors.CreateIterator(); It; ++It)
    {
        if (!It->Value || It->Value->IsActorBeingDestroyed())
        {
            Vehicles.Remove(It->Key);
            It.RemoveCurrent();
        }
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
                // Fire the data-defined default (worst-case) branch
                const FIncidentDefinition* Def = FIncidentLoader::FindDefinition(Incident.Type);
                ResolveIncident(ID, Def ? Def->DefaultBranch : 0);
                Incident.State = EIncidentState::Expired;
                GetEventBus()->OnIncidentExpired.Broadcast(ID);
            }
        }
    }

    // Prune resolved/expired incidents once they are stale (keeps HUD history briefly)
    for (auto It = Incidents.CreateIterator(); It; ++It)
    {
        const FIncidentData& Incident = It->Value;
        if ((Incident.State == EIncidentState::Resolved ||
             Incident.State == EIncidentState::Expired) &&
            CurrentTick > Incident.SpawnTick + 2000)
        {
            It.RemoveCurrent();
        }
    }
}

void USimulationSubsystem::TickVisibility()
{
    // Advance flicker state on all lights
    for (auto& [Index, Floor] : Floors)
    {
        if (Floor) Floor->TickLights(CurrentTick, SimRNG);
    }

    // Promote pending incidents that have become visible
    for (auto& [ID, Incident] : Incidents)
    {
        if (Incident.State != EIncidentState::Pending) continue;

        AParkingFloor* Floor = GetFloor(Incident.FloorIndex);
        if (!Floor) continue;

        if (Incident.TileID >= 0 &&
            Floor->GetVisibilityGrid().IsTileVisible(Incident.TileID))
        {
            const FIncidentDefinition* Def = FIncidentLoader::FindDefinition(Incident.Type);

            if (Def && Def->bRequiresDecision)
            {
                // §5 — decision card: player must pick a branch before the timer runs out
                Incident.State = EIncidentState::DecisionPending;
                Incident.DecisionDeadlineTick =
                    CurrentTick + (uint64)(Def->TimerSeconds / SIM_TICK_INTERVAL);

                GetEventBus()->OnIncidentVisible.Broadcast(Incident);
                GetEventBus()->OnDecisionRequired.Broadcast(Incident);
            }
            else
            {
                Incident.State = EIncidentState::Active;
                GetEventBus()->OnIncidentVisible.Broadcast(Incident);
            }
        }
    }
}

void USimulationSubsystem::TickStaff()
{
    DispatchStaffToIncidents();

    for (auto& [ID, Agent] : StaffActors)
    {
        if (Agent && !Agent->IsActorBeingDestroyed())
            Agent->SimTick(CurrentTick);
    }
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
    if (UEconomySubsystem* Economy = GetWorld()->GetSubsystem<UEconomySubsystem>())
        Economy->Tick(CurrentTick);
}

void USimulationSubsystem::TickCity()
{
    if (UCitySubsystem* City = GetWorld()->GetSubsystem<UCitySubsystem>())
        City->Tick(CurrentTick);
}

// ─────────────────────────────────────────────────────────────────
// INCIDENT SCHEDULING
// ─────────────────────────────────────────────────────────────────

void USimulationSubsystem::SeedIncidentSchedule()
{
    if (bIncidentScheduleSeeded) return;
    bIncidentScheduleSeeded = true;

    // One first event per definition on floor 0 — each fire re-schedules its type.
    for (const FIncidentDefinition& Def : FIncidentLoader::GetAllDefinitions())
    {
        ScheduleNextEvent(Def.Type, 0);
    }
}

void USimulationSubsystem::FirePendingEvents()
{
    while (EventQueue.Num() > 0 && EventQueue.HeapTop().ScheduledTick <= CurrentTick)
    {
        FPendingEvent E;
        EventQueue.HeapPop(E);

        AParkingFloor* Floor = GetFloor(E.FloorIndex);
        if (!Floor)
        {
            // Floor not registered yet (level still loading) — retry shortly
            E.ScheduledTick = CurrentTick + 200;
            EventQueue.HeapPush(E);
            return;   // heap top unchanged otherwise → avoid spinning
        }

        // Assign the tile at fire time based on current floor state
        int32 TileID = E.TileID >= 0 ? E.TileID : PickIncidentTile(Floor);
        if (TileID < 0)
        {
            ScheduleNextEvent(E.Type, E.FloorIndex);
            continue;
        }

        FIncidentData NewIncident;
        NewIncident.ID          = NextIncidentID++;
        NewIncident.Type        = E.Type;
        NewIncident.State       = EIncidentState::Pending;
        NewIncident.FloorIndex  = E.FloorIndex;
        NewIncident.TileID      = TileID;
        NewIncident.Severity    = E.Severity;
        NewIncident.SpawnTick   = CurrentTick;

        SetIncidentTileBlocked(NewIncident, true);

        Incidents.Add(NewIncident.ID, NewIncident);
        GetEventBus()->OnIncidentSpawned.Broadcast(NewIncident);

        // Schedule next event of this type for this floor (Poisson)
        ScheduleNextEvent(E.Type, E.FloorIndex);
    }
}

int32 USimulationSubsystem::PickIncidentTile(AParkingFloor* Floor)
{
    if (!Floor || Floor->GetTileCount() == 0) return -1;

    for (int32 Attempt = 0; Attempt < 32; ++Attempt)
    {
        const int32 Candidate = SimRNG.RandRange(0, Floor->GetTileCount() - 1);
        const FFloorTile& Tile = Floor->GetTile(Candidate);
        if (Tile.Type == ETileType::Lane && !Tile.bBlocked)
            return Candidate;
    }
    return -1;
}

void USimulationSubsystem::SetIncidentTileBlocked(FIncidentData& Incident, bool bBlocked)
{
    // Only physically-obstructing incident types block the tile.
    switch (Incident.Type)
    {
        case EIncidentType::FenderBender:
        case EIncidentType::OilSpill:
        case EIncidentType::MedicalEmergency:
        case EIncidentType::Altercation:
        case EIncidentType::StructuralCrack:
        case EIncidentType::PipeBurst:
            break;
        default:
            return;
    }

    AParkingFloor* Floor = GetFloor(Incident.FloorIndex);
    if (!Floor || !Floor->IsValidTileIndex(Incident.TileID)) return;

    FFloorTile& Tile = Floor->GetTile(Incident.TileID);
    if (Tile.bBlocked == bBlocked) return;

    Tile.bBlocked = bBlocked;
    Floor->MarkAllFlowFieldsDirty();   // vehicles re-route around the blockage

    if (Incident.Type == EIncidentType::StructuralCrack)
    {
        if (bBlocked) Floor->AddCrack();
        else          Floor->RemoveCrack();
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
    // Lambda values come from Data/IncidentTypes.json (§5)
    const FIncidentDefinition* Def = FIncidentLoader::FindDefinition(Type);
    if (!Def || Def->Lambda <= 0.f) return;

    const float Interval = SamplePoissonInterval(Def->Lambda);  // In seconds
    const uint64 DelayTicks = FMath::Max(1, FMath::RoundToInt(Interval / SIM_TICK_INTERVAL));

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
    if (Incident->State == EIncidentState::Resolved ||
        Incident->State == EIncidentState::Expired) return;

    SetIncidentTileBlocked(*Incident, false);
    Incident->State = EIncidentState::Resolved;

    GetEventBus()->OnDecisionMade.Broadcast(IncidentID, BranchIndex);
    GetEventBus()->OnIncidentResolved.Broadcast(IncidentID);

    // Faction consequences applied by UEconomySubsystem listening to OnDecisionMade
}

void USimulationSubsystem::ResolveIncidentByStaff(uint32 IncidentID)
{
    FIncidentData* Incident = Incidents.Find(IncidentID);
    if (!Incident) return;
    if (Incident->State == EIncidentState::Resolved ||
        Incident->State == EIncidentState::Expired) return;

    SetIncidentTileBlocked(*Incident, false);
    Incident->State = EIncidentState::Resolved;

    // No decision branch — staff handled it quietly.
    GetEventBus()->OnIncidentResolved.Broadcast(IncidentID);
}

uint32 USimulationSubsystem::GetFirstPendingDecisionID() const
{
    uint32 BestID = 0;
    uint64 BestDeadline = TNumericLimits<uint64>::Max();

    // Most urgent (soonest deadline) decision first
    for (const auto& [ID, Incident] : Incidents)
    {
        if (Incident.State == EIncidentState::DecisionPending &&
            Incident.DecisionDeadlineTick < BestDeadline)
        {
            BestDeadline = Incident.DecisionDeadlineTick;
            BestID = ID;
        }
    }
    return BestID;
}

// ─────────────────────────────────────────────────────────────────
// STAFF (§6)
// ─────────────────────────────────────────────────────────────────

AStaffAgent* USimulationSubsystem::HireStaff(EStaffRole Role, int32 FloorIndex)
{
    FStaffData Data;
    Data.ID            = NextStaffID;
    Data.Role          = Role;
    Data.Trait         = (EStaffTrait)SimRNG.RandRange(0, (int32)EStaffTrait::Experienced);
    Data.AssignedFloor = FloorIndex;

    return SpawnStaffActor(Data);
}

AStaffAgent* USimulationSubsystem::SpawnStaffActor(const FStaffData& Data)
{
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AStaffAgent* Agent = GetWorld()->SpawnActor<AStaffAgent>(
        AStaffAgent::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
    if (!Agent) return nullptr;

    if (Data.Fatigue > 0.f || Data.Loyalty < 1.f || Data.CurrentTileID >= 0)
        Agent->RestoreData(Data);   // save/load path — keep stats
    else
        Agent->Initialize(Data.ID, Data.Role, Data.Trait, Data.AssignedFloor);

    StaffActors.Add(Data.ID, Agent);
    NextStaffID = FMath::Max(NextStaffID, Data.ID + 1);

    UE_LOG(LogTemp, Log, TEXT("[SimulationSubsystem] Staff %u hired (%s)"),
        Data.ID, *StaticEnum<EStaffRole>()->GetNameStringByValue((int64)Data.Role));
    return Agent;
}

void USimulationSubsystem::DispatchStaffToIncidents()
{
    for (auto& [ID, Incident] : Incidents)
    {
        if (Incident.State != EIncidentState::Active) continue;
        if (Incident.AssignedStaffID != 0)            continue;

        const FIncidentDefinition* Def = FIncidentLoader::FindDefinition(Incident.Type);
        if (!Def || Def->bRequiresDecision) continue;   // decisions are the player's job

        // Find an idle staff member of the right role on the incident's floor
        for (auto& [StaffID, Agent] : StaffActors)
        {
            if (!Agent || Agent->IsActorBeingDestroyed()) continue;

            const FStaffData& Staff = Agent->GetStaffData();
            if (Staff.Role != Def->AutoResolveRole)          continue;
            if (Staff.AssignedFloor != Incident.FloorIndex)  continue;
            if (!Agent->IsIdle())                            continue;

            FStaffTask Task;
            Task.Type          = EStaffTaskType::ResolveIncident;
            Task.TargetTileID  = Incident.TileID;
            Task.IncidentID    = ID;
            // Base 10 seconds of work, scaled by trait/fatigue speed
            Task.TicksRemaining = FMath::CeilToInt(200.f / Staff.ResolutionSpeed());
            Task.bInterruptible = false;

            Agent->EnqueueTask(Task);
            Incident.AssignedStaffID = (int32)StaffID;
            Incident.State = EIncidentState::InProgress;
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// VEHICLE SPAWNING (§7)
// ─────────────────────────────────────────────────────────────────

AVehicleAgent* USimulationSubsystem::SpawnVehicleAt(EVehicleType Type, int32 FloorIndex, int32 TileID)
{
    AParkingFloor* Floor = GetFloor(FloorIndex);
    if (!Floor || !Floor->IsValidTileIndex(TileID)) return nullptr;

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AVehicleAgent* Agent = GetWorld()->SpawnActor<AVehicleAgent>(
        AVehicleAgent::StaticClass(),
        Floor->TileIndexToWorldLocation(TileID),
        FRotator::ZeroRotator,
        Params);
    if (!Agent) return nullptr;

    const uint32 VehicleID = NextVehicleID++;
    Agent->Initialize(VehicleID, Type, FloorIndex, TileID);

    FVehicleData Data = Agent->GetVehicleData();
    RegisterVehicle(VehicleID, Data, Agent);
    return Agent;
}

AVehicleAgent* USimulationSubsystem::SpawnVehicleFromCity(EVehicleType Type)
{
    AParkingFloor* Floor = GetFloor(0);
    if (!Floor) return nullptr;

    const int32 EntryTile = Floor->GetEntryGateTile();
    if (EntryTile < 0) return nullptr;

    // Don't stack arrivals on a congested gate
    if (Floor->GetVehicleCountOnTile(EntryTile) >= 2) return nullptr;

    return SpawnVehicleAt(Type, 0, EntryTile);
}

// ─────────────────────────────────────────────────────────────────
// REGISTRIES
// ─────────────────────────────────────────────────────────────────

void USimulationSubsystem::RegisterVehicle(uint32 VehicleID, FVehicleData& Data, AVehicleAgent* Actor)
{
    Data.ID = VehicleID;
    Vehicles.Add(VehicleID, Data);
    VehicleActors.Add(VehicleID, Actor);
}

void USimulationSubsystem::UnregisterVehicle(uint32 VehicleID)
{
    Vehicles.Remove(VehicleID);
    VehicleActors.Remove(VehicleID);
}

FVehicleData* USimulationSubsystem::GetVehicle(uint32 VehicleID)
{
    return Vehicles.Find(VehicleID);
}

AVehicleAgent* USimulationSubsystem::GetVehicleActor(uint32 VehicleID)
{
    AVehicleAgent** Found = VehicleActors.Find(VehicleID);
    return Found ? *Found : nullptr;
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
// SAVE/LOAD SUPPORT (§14)
// ─────────────────────────────────────────────────────────────────

void USimulationSubsystem::SetClock(uint64 Tick, int32 Day)
{
    CurrentTick = Tick;
    CurrentDay  = Day;
}

void USimulationSubsystem::ClearAllAgents()
{
    for (auto& [ID, Agent] : VehicleActors)
    {
        if (Agent && !Agent->IsActorBeingDestroyed()) Agent->Destroy();
    }
    VehicleActors.Empty();
    Vehicles.Empty();

    for (auto& [ID, Agent] : StaffActors)
    {
        if (Agent && !Agent->IsActorBeingDestroyed()) Agent->Destroy();
    }
    StaffActors.Empty();

    Incidents.Empty();
    EventQueue.Empty();
    bIncidentScheduleSeeded = false;

    for (auto& [Index, Floor] : Floors)
    {
        if (Floor)
        {
            Floor->ClearAllOccupants();
            Floor->MarkAllFlowFieldsDirty();
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// HELPERS
// ─────────────────────────────────────────────────────────────────

UEventBusSubsystem* USimulationSubsystem::GetEventBus() const
{
    return UEventBusSubsystem::Get(this);
}
