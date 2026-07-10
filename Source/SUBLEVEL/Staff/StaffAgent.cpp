#include "Staff/StaffAgent.h"
#include "Simulation/FloorGrid/ParkingFloor.h"
#include "Subsystems/SimulationSubsystem.h"
#include "Subsystems/EventBusSubsystem.h"
#include "Framework/SubLevelGameState.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"

namespace
{
    // Staff mesh height offset above the floor plane.
    constexpr float StaffZOffset = 40.f;

    FLinearColor RoleColor(EStaffRole Role)
    {
        switch (Role)
        {
            case EStaffRole::Attendant:   return FLinearColor(0.9f, 0.8f, 0.2f);  // yellow
            case EStaffRole::Security:    return FLinearColor(0.2f, 0.4f, 0.9f);  // blue
            case EStaffRole::Maintenance: return FLinearColor(0.9f, 0.4f, 0.1f);  // orange
            default:                      return FLinearColor::White;
        }
    }
}

AStaffAgent::AStaffAgent()
{
    PrimaryActorTick.bCanEverTick = true;

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaffMesh"));
    RootComponent = MeshComponent;
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MeshComponent->SetRelativeScale3D(FVector(0.35f, 0.35f, 0.8f));

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube"));
    if (CubeMesh.Succeeded())
        MeshComponent->SetStaticMesh(CubeMesh.Object);
}

void AStaffAgent::Initialize(uint32 InStaffID, EStaffRole InRole, EStaffTrait InTrait, int32 AssignedFloor)
{
    StaffData.ID            = InStaffID;
    StaffData.Role          = InRole;
    StaffData.Trait         = InTrait;
    StaffData.AssignedFloor = AssignedFloor;
    StaffData.Fatigue       = 0.f;
    StaffData.Loyalty       = 1.f;
    StaffData.Name          = FString::Printf(TEXT("Staff %u"), InStaffID);

    if (UMaterialInstanceDynamic* MID = MeshComponent->CreateAndSetMaterialInstanceDynamic(0))
        MID->SetVectorParameterValue(TEXT("Color"), RoleColor(InRole));

    if (AParkingFloor* Floor = GetFloor())
    {
        if (StaffData.CurrentTileID < 0)
            StaffData.CurrentTileID = Floor->GetEntryGateTile();

        WorldPosLast = TileToWorldLoc(Floor, StaffData.CurrentTileID);
        WorldPosNext = WorldPosLast;
        SetActorLocation(WorldPosLast);
    }
}

void AStaffAgent::RestoreData(const FStaffData& InData)
{
    StaffData = InData;
    Initialize(InData.ID, InData.Role, InData.Trait, InData.AssignedFloor);
    StaffData = InData;   // Initialize resets stats; put the saved values back

    if (AParkingFloor* Floor = GetFloor())
    {
        WorldPosLast = TileToWorldLoc(Floor, StaffData.CurrentTileID);
        WorldPosNext = WorldPosLast;
        SetActorLocation(WorldPosLast);
    }
}

void AStaffAgent::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    InterpAlpha = FMath::Clamp(InterpAlpha + DeltaTime / SimTickInterval, 0.f, 1.f);
    SetActorLocation(FMath::Lerp(WorldPosLast, WorldPosNext, InterpAlpha));
}

void AStaffAgent::EnqueueTask(const FStaffTask& Task)
{
    StaffData.TaskQueue.Add(Task);

    if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
        Bus->OnStaffTaskChanged.Broadcast(StaffData.ID, Task.Type);
}

// ─────────────────────────────────────────────────────────────────
// SIM TICK
// ─────────────────────────────────────────────────────────────────

void AStaffAgent::SimTick(uint64 CurrentTick)
{
    WorldPosLast = WorldPosNext;
    InterpAlpha  = 0.f;

    if (StaffData.TaskQueue.Num() > 0)
    {
        ProcessTask(CurrentTick);
        TickStats(/*bWorking =*/ true);
    }
    else
    {
        ProcessPatrol(CurrentTick);
        TickStats(/*bWorking =*/ false);
    }
}

void AStaffAgent::ProcessTask(uint64 CurrentTick)
{
    AParkingFloor* Floor = GetFloor();
    if (!Floor) return;

    FStaffTask& Task = StaffData.TaskQueue[0];

    // Drop tasks whose incident has already been handled (expired, player-resolved).
    if (Task.Type == EStaffTaskType::ResolveIncident)
    {
        USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>();
        const FIncidentData* Incident = Sim ? Sim->FindIncident(Task.IncidentID) : nullptr;
        if (!Incident ||
            Incident->State == EIncidentState::Resolved ||
            Incident->State == EIncidentState::Expired)
        {
            StaffData.TaskQueue.RemoveAt(0);
            return;
        }
    }

    // Travel phase
    if (StaffData.CurrentTileID != Task.TargetTileID && Task.TargetTileID >= 0)
    {
        if (MoveCooldown > 0.f) { --MoveCooldown; return; }

        const int32 NextTile = StepToward(Floor, Task.TargetTileID);
        if (NextTile >= 0)
        {
            MoveToTile(Floor, NextTile);

            float SpeedMod = (StaffData.Trait == EStaffTrait::Slow) ? 0.8f : 1.0f;
            MoveCooldown = TicksPerTile / SpeedMod;
        }
        return;
    }

    // On-site phase — count down work remaining
    if (Task.TicksRemaining > 0)
    {
        --Task.TicksRemaining;
        return;
    }

    // Work complete
    if (Task.Type == EStaffTaskType::ResolveIncident)
    {
        if (USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>())
            Sim->ResolveIncidentByStaff(Task.IncidentID);
    }

    StaffData.TaskQueue.RemoveAt(0);
}

void AStaffAgent::ProcessPatrol(uint64 CurrentTick)
{
    AParkingFloor* Floor = GetFloor();
    if (!Floor || Floor->GetTileCount() == 0) return;

    USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>();
    if (!Sim) return;

    // Re-target every ~10 seconds of sim time
    if (PatrolTargetTile < 0 || CurrentTick >= NextPatrolRetarget ||
        StaffData.CurrentTileID == PatrolTargetTile)
    {
        // Pick a random passable lane tile to wander toward
        for (int32 Attempt = 0; Attempt < 16; ++Attempt)
        {
            const int32 Candidate = Sim->GetRNG().RandRange(0, Floor->GetTileCount() - 1);
            const FFloorTile& Tile = Floor->GetTile(Candidate);
            if (Tile.Type == ETileType::Lane && Tile.IsPassable())
            {
                PatrolTargetTile = Candidate;
                break;
            }
        }
        NextPatrolRetarget = CurrentTick + 200;
    }

    if (PatrolTargetTile < 0) return;
    if (MoveCooldown > 0.f) { --MoveCooldown; return; }

    const int32 NextTile = StepToward(Floor, PatrolTargetTile);
    if (NextTile >= 0)
    {
        MoveToTile(Floor, NextTile);

        float SpeedMod = (StaffData.Trait == EStaffTrait::Slow) ? 0.8f : 1.0f;
        MoveCooldown = TicksPerTile / SpeedMod;
    }
    else
    {
        PatrolTargetTile = -1;   // stuck — re-target next tick
    }
}

void AStaffAgent::TickStats(bool bWorking)
{
    // ── Fatigue ──────────────────────────────────────────────────
    float FatigueRate = bWorking ? 0.0005f : -0.0002f;
    if (bWorking && StaffData.Trait == EStaffTrait::Diligent)
        FatigueRate *= 0.7f;   // accumulates 30% slower

    StaffData.Fatigue = FMath::Clamp(StaffData.Fatigue + FatigueRate, 0.f, 1.f);

    if (StaffData.Fatigue >= 0.8f && !bFatigueNotified)
    {
        bFatigueNotified = true;
        if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
            Bus->OnStaffFatigued.Broadcast(StaffData.ID);
    }
    else if (StaffData.Fatigue < 0.6f)
    {
        bFatigueNotified = false;   // re-arm after recovery
    }

    // ── Loyalty ──────────────────────────────────────────────────
    if (StaffData.Trait == EStaffTrait::EasilyBribed)
    {
        const ASubLevelGameState* GS = GetWorld()->GetGameState<ASubLevelGameState>();
        if (GS && GS->GetFactionScore(EFactionType::ShadowClients) >= 20.f)
        {
            StaffData.Loyalty = FMath::Clamp(StaffData.Loyalty - 0.0001f, 0.f, 1.f);

            if (StaffData.Loyalty <= 0.f && !bCompromiseNotified)
            {
                bCompromiseNotified = true;
                if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
                    Bus->OnStaffCompromised.Broadcast(StaffData.ID);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// MOVEMENT
// ─────────────────────────────────────────────────────────────────

int32 AStaffAgent::StepToward(AParkingFloor* Floor, int32 TargetTile)
{
    if (!Floor || !Floor->IsValidTileIndex(TargetTile) ||
        !Floor->IsValidTileIndex(StaffData.CurrentTileID))
        return -1;

    const int32 W = Floor->GetGridWidth();
    const int32 CurRow = StaffData.CurrentTileID / W;
    const int32 CurCol = StaffData.CurrentTileID % W;
    const int32 TgtRow = TargetTile / W;
    const int32 TgtCol = TargetTile % W;

    const int32 DRow = FMath::Clamp(TgtRow - CurRow, -1, 1);
    const int32 DCol = FMath::Clamp(TgtCol - CurCol, -1, 1);

    // Prefer the axis with the greater remaining distance; fall back to the other.
    const bool bRowFirst = FMath::Abs(TgtRow - CurRow) >= FMath::Abs(TgtCol - CurCol);

    const int32 CandidateA = bRowFirst ? Floor->TileIndex(CurRow + DRow, CurCol)
                                       : Floor->TileIndex(CurRow, CurCol + DCol);
    const int32 CandidateB = bRowFirst ? Floor->TileIndex(CurRow, CurCol + DCol)
                                       : Floor->TileIndex(CurRow + DRow, CurCol);

    for (int32 Candidate : { CandidateA, CandidateB })
    {
        if (Candidate == StaffData.CurrentTileID) continue;
        if (!Floor->IsValidTileIndex(Candidate))  continue;

        // Staff may always enter their destination tile (incidents block it for vehicles).
        const FFloorTile& Tile = Floor->GetTile(Candidate);
        if (Candidate == TargetTile || Tile.IsPassable())
            return Candidate;
    }
    return -1;
}

void AStaffAgent::MoveToTile(AParkingFloor* Floor, int32 NextTile)
{
    if (!Floor || NextTile == StaffData.CurrentTileID) return;

    StaffData.CurrentTileID = NextTile;
    WorldPosNext = TileToWorldLoc(Floor, NextTile);
}

// ─────────────────────────────────────────────────────────────────
// HELPERS
// ─────────────────────────────────────────────────────────────────

AParkingFloor* AStaffAgent::GetFloor() const
{
    if (USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>())
        return Sim->GetFloor(StaffData.AssignedFloor);
    return nullptr;
}

FVector AStaffAgent::TileToWorldLoc(AParkingFloor* Floor, int32 TileIdx) const
{
    if (!Floor || !Floor->IsValidTileIndex(TileIdx)) return GetActorLocation();
    return Floor->TileIndexToWorldLocation(TileIdx) + FVector(0, 0, StaffZOffset);
}
