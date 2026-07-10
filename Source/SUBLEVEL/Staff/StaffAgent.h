#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SubLevelTypes.h"
#include "StaffAgent.generated.h"

class AParkingFloor;

// §6 — Staff AI System
// Per-employee actor with task queue, fatigue/loyalty state, and patrol behavior.
// Sim logic runs in SimTick (called by USimulationSubsystem::TickStaff);
// the engine tick only interpolates the mesh between sim positions.

UCLASS()
class SUBLEVEL_API AStaffAgent : public AActor
{
    GENERATED_BODY()

public:
    AStaffAgent();

    void Initialize(uint32 InStaffID, EStaffRole InRole, EStaffTrait InTrait, int32 AssignedFloor);

    // Restores full stat block (save/load path).
    void RestoreData(const FStaffData& InData);

    virtual void Tick(float DeltaTime) override;

    // Called by USimulationSubsystem::TickStaff
    void SimTick(uint64 CurrentTick);

    const FStaffData& GetStaffData() const { return StaffData; }

    // ── Task queue ────────────────────────────────────────────────
    void EnqueueTask(const FStaffTask& Task);

    // True when the agent has no task other than (implicit) patrol.
    bool IsIdle() const { return StaffData.TaskQueue.Num() == 0; }

private:
    FStaffData StaffData;

    // ── Visual interpolation (same pattern as AVehicleAgent) ─────
    FVector WorldPosLast = FVector::ZeroVector;
    FVector WorldPosNext = FVector::ZeroVector;
    float   InterpAlpha  = 0.f;
    static constexpr float SimTickInterval = SubLevelSim::TickInterval;

    // ── Movement ─────────────────────────────────────────────────
    // Staff walk slower than vehicles drive.
    static constexpr float TicksPerTile = 6.0f;
    float  MoveCooldown       = 0.f;
    uint64 NextPatrolRetarget = 0;
    int32  PatrolTargetTile   = -1;

    // Greedy one-tile step toward TargetTile; returns new tile or -1 if stuck.
    int32 StepToward(AParkingFloor* Floor, int32 TargetTile);
    void  MoveToTile(AParkingFloor* Floor, int32 NextTile);

    // ── Behavior phases ──────────────────────────────────────────
    void ProcessTask(uint64 CurrentTick);
    void ProcessPatrol(uint64 CurrentTick);
    void TickStats(bool bWorking);

    bool bFatigueNotified    = false;
    bool bCompromiseNotified = false;

    AParkingFloor* GetFloor() const;
    FVector TileToWorldLoc(AParkingFloor* Floor, int32 TileIdx) const;

    UPROPERTY()
    UStaticMeshComponent* MeshComponent = nullptr;
};
