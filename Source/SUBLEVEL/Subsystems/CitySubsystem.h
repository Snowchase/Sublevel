#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SubLevelTypes.h"
#include "CitySubsystem.generated.h"

// §7 — City Context System
// Manages the demand curve, city event calendar, and weather state.
// Implemented in a future pass.

UCLASS()
class SUBLEVEL_API UCitySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    float GetCurrentDemandLambda() const { return CurrentLambda; }
    bool  IsRainActive()           const { return bRainActive; }
    bool  HasActiveCityEvent(ECityEventType Type) const;

    // Called by SimulationSubsystem::TickCity
    void Tick(uint64 CurrentTick);

private:
    float CurrentLambda = 2.0f; // vehicles/minute
    bool  bRainActive   = false;

    TArray<FCityEventData> ActiveEvents;
};
