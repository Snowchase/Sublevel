#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SubLevelTypes.h"
#include "StaffAgent.generated.h"

// §6 — Staff AI System
// Per-employee actor with task queue, fatigue/loyalty state, and patrol behavior.
// Implemented in a future pass.

UCLASS()
class SUBLEVEL_API AStaffAgent : public AActor
{
    GENERATED_BODY()

public:
    AStaffAgent();

    void Initialize(uint32 InStaffID, EStaffRole Role, EStaffTrait Trait, int32 AssignedFloor);

    // Called by USimulationSubsystem::TickStaff
    void SimTick(uint64 CurrentTick);

    const FStaffData& GetStaffData() const { return StaffData; }

private:
    FStaffData StaffData;
};
