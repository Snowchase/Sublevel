#include "Subsystems/EconomySubsystem.h"
#include "Subsystems/EventBusSubsystem.h"

void UEconomySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    SubscribeToEventBus();
}

void UEconomySubsystem::Deinitialize()
{
    Super::Deinitialize();
}

void UEconomySubsystem::SubscribeToEventBus()
{
    if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
    {
        Bus->OnVehicleExited.AddDynamic(this, &UEconomySubsystem::OnVehicleExited);
        Bus->OnDecisionMade.AddDynamic(this, &UEconomySubsystem::OnDecisionMade);
    }
}

void UEconomySubsystem::OnVehicleExited(uint32 VehicleID, float Revenue)
{
    Balance            += Revenue;
    DailyAccumulator   += Revenue;

    if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
        Bus->OnRevenueEarned.Broadcast(Revenue);
}

void UEconomySubsystem::OnDecisionMade(uint32 IncidentID, int32 BranchIndex)
{
    // §10 — apply faction deltas from incident branch definition
}

void UEconomySubsystem::Tick(uint64 CurrentTick)
{
    // §10 — day boundary reset, loan repayment check
}
