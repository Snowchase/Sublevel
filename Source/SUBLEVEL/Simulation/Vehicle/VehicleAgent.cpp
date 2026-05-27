#include "Simulation/Vehicle/VehicleAgent.h"
#include "Simulation/FloorGrid/ParkingFloor.h"
#include "Subsystems/SimulationSubsystem.h"
#include "Subsystems/EventBusSubsystem.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"

// ─────────────────────────────────────────────────────────────────
// LIFECYCLE
// ─────────────────────────────────────────────────────────────────

AVehicleAgent::AVehicleAgent()
{
    PrimaryActorTick.bCanEverTick = true;

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VehicleMesh"));
    RootComponent = MeshComponent;
}

void AVehicleAgent::Initialize(uint32 InVehicleID, EVehicleType InType, int32 InFloorIndex, int32 StartTileID)
{
    VehicleData.ID          = InVehicleID;
    VehicleData.Type        = InType;
    VehicleData.State       = EVehicleState::Spawning;
    VehicleData.FloorIndex  = InFloorIndex;
    VehicleData.CurrentTileID = StartTileID;

    if (AParkingFloor* Floor = GetFloor())
    {
        WorldPosLast = TileToWorldLoc(Floor, StartTileID);
        WorldPosNext = WorldPosLast;
        SetActorLocation(WorldPosLast);

        Floor->AddVehicleToTile(StartTileID, InVehicleID);
    }
}

// ─────────────────────────────────────────────────────────────────
// ENGINE TICK — visual interpolation only
// ─────────────────────────────────────────────────────────────────

void AVehicleAgent::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Advance interpolation alpha based on real time elapsed since last SimTick.
    InterpAlpha = FMath::Clamp(InterpAlpha + DeltaTime / SimTickInterval, 0.f, 1.f);

    const FVector InterpPos = FMath::Lerp(WorldPosLast, WorldPosNext, InterpAlpha);
    SetActorLocation(InterpPos);

    // Orient mesh toward direction of travel
    const FVector Delta = WorldPosNext - WorldPosLast;
    if (!Delta.IsNearlyZero())
    {
        const FRotator TargetRot = Delta.Rotation();
        SetActorRotation(FMath::RInterpTo(GetActorRotation(), TargetRot, DeltaTime, 12.f));
    }
}

// ─────────────────────────────────────────────────────────────────
// SIM TICK — game logic, called by USimulationSubsystem
// ─────────────────────────────────────────────────────────────────

void AVehicleAgent::SimTick(uint64 CurrentTick)
{
    // Reset interpolation for the new sim step
    WorldPosLast  = WorldPosNext;
    InterpAlpha   = 0.f;

    switch (VehicleData.State)
    {
        case EVehicleState::Spawning:  FSM_Spawning(CurrentTick);  break;
        case EVehicleState::Seeking:   FSM_Seeking(CurrentTick);   break;
        case EVehicleState::Queuing:   FSM_Queuing(CurrentTick);   break;
        case EVehicleState::Parking:   FSM_Parking(CurrentTick);   break;
        case EVehicleState::Parked:    FSM_Parked(CurrentTick);    break;
        case EVehicleState::Circling:  FSM_Circling(CurrentTick);  break;
        case EVehicleState::Exiting:   FSM_Exiting(CurrentTick);   break;
        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────
// FSM STATE HANDLERS
// ─────────────────────────────────────────────────────────────────

void AVehicleAgent::FSM_Spawning(uint64 CurrentTick)
{
    if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
        Bus->OnVehicleArrived.Broadcast(VehicleData.ID);

    TransitionTo(EVehicleState::Seeking, CurrentTick);
}

void AVehicleAgent::FSM_Seeking(uint64 CurrentTick)
{
    AParkingFloor* Floor = GetFloor();
    if (!Floor) { TransitionTo(EVehicleState::Exiting, CurrentTick); return; }

    // Try to reserve a stall on first seek tick
    if (ReservedStallTileID == -1)
    {
        ReservedStallTileID = FindNearestAvailableStall(Floor);

        if (ReservedStallTileID == -1)
        {
            ++TicksWithoutStall;
            if (TicksWithoutStall >= SeekTimeoutTicks)
                TransitionTo(EVehicleState::Circling, CurrentTick);
            return;
        }

        // Reserve the stall tile
        Floor->GetTile(ReservedStallTileID).bOccupied = true;
        Floor->GetTile(ReservedStallTileID).OccupantID = VehicleData.ID;
        VehicleData.TargetTileID = ReservedStallTileID;
    }

    // Move toward reserved stall via its flow field
    if (MoveCooldown > 0.f) { --MoveCooldown; return; }

    const int32 NextTile = SampleFlowFieldNextTile(Floor, (uint32)ReservedStallTileID);

    if (NextTile == -1)
    {
        TransitionTo(EVehicleState::Queuing, CurrentTick);
        return;
    }

    if (NextTile == ReservedStallTileID)
    {
        // Arrived at stall entry — begin parking maneuver
        MoveToTile(Floor, NextTile);
        TransitionTo(EVehicleState::Parking, CurrentTick);
        return;
    }

    // Check for congestion on the next tile
    if (Floor->GetVehicleCountOnTile(NextTile) >= 2)
    {
        TransitionTo(EVehicleState::Queuing, CurrentTick);
        return;
    }

    MoveToTile(Floor, NextTile);
    MoveCooldown = TicksPerTile / VehicleData.SpeedModifier;
}

void AVehicleAgent::FSM_Queuing(uint64 CurrentTick)
{
    AParkingFloor* Floor = GetFloor();
    if (!Floor) { TransitionTo(EVehicleState::Exiting, CurrentTick); return; }

    // Wait until adjacent tile clears
    if (ReservedStallTileID != -1)
    {
        const int32 NextTile = SampleFlowFieldNextTile(Floor, (uint32)ReservedStallTileID);
        if (NextTile != -1 && Floor->GetVehicleCountOnTile(NextTile) < 2)
        {
            TransitionTo(EVehicleState::Seeking, CurrentTick);
        }
    }
    else
    {
        TransitionTo(EVehicleState::Seeking, CurrentTick);
    }
}

void AVehicleAgent::FSM_Parking(uint64 CurrentTick)
{
    // One-tick maneuver: snap to stall center, mark parked
    AParkingFloor* Floor = GetFloor();
    if (!Floor) { TransitionTo(EVehicleState::Exiting, CurrentTick); return; }

    WorldPosNext = TileToWorldLoc(Floor, VehicleData.CurrentTileID);

    VehicleData.ParkStartTick = CurrentTick;

    // Draw duration from seeded RNG via SimulationSubsystem
    if (USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>())
    {
        const int32 DurationMin = 400;
        const int32 DurationMax = 4800;
        VehicleData.DespawnTick = CurrentTick + Sim->GetRNG().RandRange(DurationMin, DurationMax);
    }

    if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
        Bus->OnVehicleParked.Broadcast(VehicleData.ID, VehicleData.CurrentTileID);

    TransitionTo(EVehicleState::Parked, CurrentTick);
}

void AVehicleAgent::FSM_Parked(uint64 CurrentTick)
{
    if (CurrentTick >= VehicleData.DespawnTick)
        TransitionTo(EVehicleState::Exiting, CurrentTick);
}

void AVehicleAgent::FSM_Circling(uint64 CurrentTick)
{
    // Follow exit flow field — give up and leave
    AParkingFloor* Floor = GetFloor();
    if (!Floor) { TransitionTo(EVehicleState::Despawned, CurrentTick); return; }

    if (MoveCooldown > 0.f) { --MoveCooldown; return; }

    // Find exit gate tile — sample exit flow field
    // For now route toward tile 0 (EntryGate fallback) until exit gates are placed
    const int32 NextTile = SampleFlowFieldNextTile(Floor, 0);
    if (NextTile == -1 || NextTile == VehicleData.CurrentTileID)
    {
        TransitionTo(EVehicleState::Exiting, CurrentTick);
        return;
    }

    MoveToTile(Floor, NextTile);
    MoveCooldown = TicksPerTile / VehicleData.SpeedModifier;
}

void AVehicleAgent::FSM_Exiting(uint64 CurrentTick)
{
    AParkingFloor* Floor = GetFloor();
    if (!Floor)
    {
        if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
        {
            const float Revenue = 0.f;
            Bus->OnVehicleExited.Broadcast(VehicleData.ID, Revenue);
        }
        TransitionTo(EVehicleState::Despawned, CurrentTick);
        return;
    }

    if (MoveCooldown > 0.f) { --MoveCooldown; return; }

    // Sample the exit flow field — exit gates register their tile as the destination
    const int32 NextTile = SampleFlowFieldNextTile(Floor, 0);

    if (NextTile == -1)
    {
        // Reached exit or no path — despawn
        const float ParkSeconds = VehicleData.ParkStartTick > 0
            ? (CurrentTick - VehicleData.ParkStartTick) * 0.05f
            : 0.f;
        const float HourlyRate  = 4.0f; // $4/hr base, will be replaced by EconomySubsystem
        const float Revenue     = (ParkSeconds / 3600.f) * HourlyRate;

        if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
            Bus->OnVehicleExited.Broadcast(VehicleData.ID, Revenue);

        RemoveVehicleFromFloor(Floor);
        TransitionTo(EVehicleState::Despawned, CurrentTick);
        return;
    }

    MoveToTile(Floor, NextTile);
    MoveCooldown = TicksPerTile / VehicleData.SpeedModifier;
}

// ─────────────────────────────────────────────────────────────────
// TRANSITION
// ─────────────────────────────────────────────────────────────────

void AVehicleAgent::TransitionTo(EVehicleState NewState, uint64 CurrentTick)
{
    VehicleData.State = NewState;

    if (NewState == EVehicleState::Despawned)
    {
        if (USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>())
            Sim->UnregisterVehicle(VehicleData.ID);

        Destroy();
    }
}

// ─────────────────────────────────────────────────────────────────
// MOVEMENT HELPERS
// ─────────────────────────────────────────────────────────────────

int32 AVehicleAgent::SampleFlowFieldNextTile(AParkingFloor* Floor, uint32 DestTileID) const
{
    if (!Floor) return -1;

    const FFlowField* Field = Floor->GetFlowFieldForDest(DestTileID);
    if (!Field || !Field->bReady) return -1;

    const int32 CurrentIdx = VehicleData.CurrentTileID;
    if (!Field->Directions.IsValidIndex(CurrentIdx)) return -1;

    const FVector2D Dir = Field->Directions[CurrentIdx];
    if (Dir.IsNearlyZero()) return -1;

    const int32 Col     = CurrentIdx % Floor->GetGridWidth();
    const int32 Row     = CurrentIdx / Floor->GetGridWidth();
    const int32 NextRow = Row + FMath::RoundToInt(Dir.Y);
    const int32 NextCol = Col + FMath::RoundToInt(Dir.X);

    if (NextRow < 0 || NextRow >= Floor->GetGridHeight() ||
        NextCol < 0 || NextCol >= Floor->GetGridWidth())
        return -1;

    return NextRow * Floor->GetGridWidth() + NextCol;
}

void AVehicleAgent::MoveToTile(AParkingFloor* Floor, int32 NextTileID)
{
    if (!Floor || NextTileID == VehicleData.CurrentTileID) return;

    Floor->RemoveVehicleFromTile(VehicleData.CurrentTileID, VehicleData.ID);
    Floor->AddVehicleToTile(NextTileID, VehicleData.ID);

    VehicleData.CurrentTileID = NextTileID;
    WorldPosNext = TileToWorldLoc(Floor, NextTileID);
}

void AVehicleAgent::RemoveVehicleFromFloor(AParkingFloor* Floor)
{
    if (!Floor) return;
    Floor->RemoveVehicleFromTile(VehicleData.CurrentTileID, VehicleData.ID);
    VehicleData.CurrentTileID = -1;
}

int32 AVehicleAgent::FindNearestAvailableStall(AParkingFloor* Floor) const
{
    if (!Floor) return -1;

    // Determine which stall types this vehicle can use
    TArray<ETileType> ValidStalls;
    switch (VehicleData.Type)
    {
        case EVehicleType::Compact:
            ValidStalls = { ETileType::Stall_Compact, ETileType::Stall_Standard };
            break;
        case EVehicleType::Standard:
            ValidStalls = { ETileType::Stall_Standard };
            break;
        case EVehicleType::SUV:
        case EVehicleType::DeliveryVan:
            ValidStalls = { ETileType::Stall_Oversized };
            break;
        case EVehicleType::Motorcycle:
            ValidStalls = { ETileType::Stall_Motorcycle };
            break;
        case EVehicleType::Disabled:
            ValidStalls = { ETileType::Stall_Disabled };
            break;
    }

    int32   BestTile = -1;
    float   BestDist = TNumericLimits<float>::Max();
    const FVector CurrentPos = TileToWorldLoc(Floor, VehicleData.CurrentTileID);

    for (int32 Idx = 0; Idx < Floor->GetTileCount(); ++Idx)
    {
        const FFloorTile& Tile = Floor->GetTile(Idx);
        if (!Tile.IsStall() || Tile.bOccupied || Tile.bBlocked)
            continue;

        if (!ValidStalls.Contains(Tile.Type))
            continue;

        const float Dist = FVector::DistSquared(CurrentPos, Floor->TileIndexToWorldLocation(Idx));
        if (Dist < BestDist)
        {
            BestDist = Dist;
            BestTile = Idx;
        }
    }

    return BestTile;
}

FVector AVehicleAgent::TileToWorldLoc(AParkingFloor* Floor, int32 TileIdx) const
{
    return Floor ? Floor->TileIndexToWorldLocation(TileIdx) : FVector::ZeroVector;
}

AParkingFloor* AVehicleAgent::GetFloor() const
{
    if (USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>())
        return Sim->GetFloor(VehicleData.FloorIndex);
    return nullptr;
}
