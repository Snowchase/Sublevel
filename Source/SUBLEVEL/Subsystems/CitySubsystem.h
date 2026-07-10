#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SubLevelTypes.h"
#include "CitySubsystem.generated.h"

// §7 — City Context System
// Manages the demand curve (Data/VehicleArrivals.json), the city event
// calendar (Data/CityEvents.json), weather state, and vehicle arrivals.

// Parsed city event definition (plain struct — not reflected).
struct FCityEventDef
{
    ECityEventType Type              = ECityEventType::StadiumNight;
    FString        Scheduling;                 // "scheduled" | "stochastic" | "faction_triggered"
    float          Lambda            = 0.f;    // stochastic: per-tick start probability
    float          DemandMultiplier  = 1.f;
    int32          DurationTicks     = 0;
    EFactionType   TriggerFaction    = EFactionType::CityAuthority;
    float          TriggerThreshold  = 0.f;    // >= 0 → fires above; < 0 → fires below
    bool           bSurgePricing     = false;
    int32          SpikeVehicles     = 0;      // burst arrivals at event start
    int32          SpikeWindowTicks  = 0;
};

UCLASS()
class SUBLEVEL_API UCitySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
    {
        return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
    }

    float GetCurrentDemandLambda() const { return CurrentLambda; }   // vehicles/minute
    bool  IsRainActive()           const { return bRainActive; }
    bool  HasActiveCityEvent(ECityEventType Type) const;
    bool  IsSurgePricingActive()   const;

    const TArray<FCityEventData>& GetActiveEvents() const { return ActiveEvents; }

    // Called by SimulationSubsystem::TickCity
    void Tick(uint64 CurrentTick);

private:
    float CurrentLambda = 2.0f; // vehicles/minute
    bool  bRainActive   = false;

    // ── Demand curve ─────────────────────────────────────────────
    TArray<float> WeekdayCurve;   // 24 entries, vehicles/minute per hour
    TArray<float> WeekendCurve;

    // ── City events ──────────────────────────────────────────────
    TArray<FCityEventDef>  EventDefs;
    TArray<FCityEventData> ActiveEvents;
    TMap<ECityEventType, uint64> EventCooldownUntil;

    // ── Vehicle arrivals ─────────────────────────────────────────
    float  SpawnAccumulator     = 0.f;
    float  SpikeAccumulator     = 0.f;
    int32  SpikeVehiclesRemaining = 0;
    uint64 SpikeEndTick         = 0;

    bool bDataLoaded = false;
    void LoadData();

    void UpdateDemand(uint64 CurrentTick);
    void TickEventCalendar(uint64 CurrentTick);
    void TickVehicleArrivals(uint64 CurrentTick);

    void StartEvent(const FCityEventDef& Def, uint64 CurrentTick);
    void EndEvent(int32 ActiveIndex, uint64 CurrentTick);

    const FCityEventDef* FindDef(ECityEventType Type) const;
    float GetFactionScoreSafe(EFactionType Faction) const;
};
