#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SubLevelTypes.h"
#include "EconomySubsystem.generated.h"

// §10 — Economy & Faction System
// Revenue calculation, loan management, shadow income, faction scoring.
// Subscribes to EventBus — never called directly by other systems
// (except the fee calculator, which vehicles query on exit).

UCLASS()
class SUBLEVEL_API UEconomySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
    {
        return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
    }

    // Called by SimulationSubsystem::TickEconomy
    void Tick(uint64 CurrentTick);

    float GetCurrentBalance()    const { return Balance; }
    float GetLoanPrincipal()     const { return LoanPrincipal; }
    float GetDailyRevenue()      const { return DailyRevenue; }

    // Parking fee for a completed stay. Applies hourly rate, per-type
    // revenue modifier, and surge pricing when a surge event is active.
    float CalculateParkingFee(float ParkedSeconds, EVehicleType Type) const;

    UFUNCTION(BlueprintCallable)
    void TakeLoan(float Amount);

    // §14 save/load
    void SetEconomyState(float InBalance, float InLoanPrincipal, float InDailyRevenue);

    // EventBus callbacks — UFUNCTION required for AddDynamic
    UFUNCTION() void OnVehicleExited(uint32 VehicleID, float Revenue);
    UFUNCTION() void OnDecisionMade(uint32 IncidentID, int32 BranchIndex);

private:
    float Balance       = 10000.f;  // Starting balance
    float LoanPrincipal = 0.f;
    float DailyRevenue  = 0.f;      // Resets at each day boundary

    // ── Tuning ───────────────────────────────────────────────────
    static constexpr float HourlyRateBase       = 4.0f;
    static constexpr float SurgeMultiplier      = 1.5f;
    static constexpr float LoanInterestRate     = 0.02f;   // daily, on principal
    static constexpr float LoanRepaymentFraction = 0.05f;  // of principal, daily
    static constexpr float ShadowIncomePerTick  = 0.05f;   // BlackMarketNight passive income
    static constexpr float DefaultThreshold     = -5000.f; // balance below this = default

    int32 LastProcessedDay = 1;
    bool  bLoanDefaulted   = false;

    void ProcessDayBoundary(int32 NewDay);
    void ApplyConsequence(const FString& Tag);
    void MirrorToGameState();
    void SubscribeToEventBus();

    float StaffDailyWage(EStaffRole Role) const;
};
