#include "Subsystems/CitySubsystem.h"

void UCitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

bool UCitySubsystem::HasActiveCityEvent(ECityEventType Type) const
{
    for (const FCityEventData& E : ActiveEvents)
    {
        if (E.Type == Type && E.bIsActive) return true;
    }
    return false;
}

void UCitySubsystem::Tick(uint64 CurrentTick)
{
    // §7 — demand curve sampling, city event calendar logic implemented here
}
