#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SubLevelTypes.h"
#include "EconomySubsystem.generated.h"

// §10 — Economy & Faction System
// Revenue calculation, loan management, shadow income, faction scoring.
// Subscribes to EventBus — never called directly by other systems.
// Implemented in a future pass.

UCLASS()
class SUBLEVEL_API UEconomySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Called by SimulationSubsystem::TickEconomy
    void Tick(uint64 CurrentTick);

    float GetCurrentBalance()    const { return Balance; }
    float GetLoanPrincipal()     const { return LoanPrincipal; }
    float GetDailyRevenue()      const { return DailyRevenue; }

private:
    float Balance      = 10000.f;  // Starting balance
    float LoanPrincipal = 0.f;
    float LoanRate      = 0.f;
    float DailyRevenue  = 0.f;
    float DailyAccumulator = 0.f;

    void SubscribeToEventBus();

    UFUNCTION() void OnVehicleExited(uint32 VehicleID, float Revenue);
    UFUNCTION() void OnDecisionMade(uint32 IncidentID, int32 BranchIndex);
};
