#include "Subsystems/CitySubsystem.h"
#include "Subsystems/SimulationSubsystem.h"
#include "Subsystems/EventBusSubsystem.h"
#include "Simulation/Vehicle/VehicleTypeLoader.h"
#include "Framework/SubLevelGameState.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"

void UCitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LoadData();
}

// ─────────────────────────────────────────────────────────────────
// DATA LOADING
// ─────────────────────────────────────────────────────────────────

void UCitySubsystem::LoadData()
{
    if (bDataLoaded) return;
    bDataLoaded = true;

    // ── Demand curve ─────────────────────────────────────────────
    {
        const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("VehicleArrivals.json"));
        FString JsonRaw;
        if (FFileHelper::LoadFileToString(JsonRaw, *Path))
        {
            TSharedPtr<FJsonObject> Root;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonRaw);
            if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
            {
                for (const TCHAR* Key : { TEXT("weekday"), TEXT("weekend") })
                {
                    const TArray<TSharedPtr<FJsonValue>>* Hours = nullptr;
                    if (Root->TryGetArrayField(Key, Hours))
                    {
                        TArray<float>& Curve =
                            FCString::Strcmp(Key, TEXT("weekday")) == 0 ? WeekdayCurve : WeekendCurve;
                        for (const TSharedPtr<FJsonValue>& Value : *Hours)
                            Curve.Add((float)Value->AsNumber());
                    }
                }
            }
        }
        UE_LOG(LogTemp, Log, TEXT("[CitySubsystem] Demand curve: %d weekday, %d weekend hours"),
            WeekdayCurve.Num(), WeekendCurve.Num());
    }

    // ── City events ──────────────────────────────────────────────
    {
        const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("CityEvents.json"));
        FString JsonRaw;
        if (FFileHelper::LoadFileToString(JsonRaw, *Path))
        {
            TArray<TSharedPtr<FJsonValue>> Root;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonRaw);
            if (FJsonSerializer::Deserialize(Reader, Root))
            {
                for (const TSharedPtr<FJsonValue>& Entry : Root)
                {
                    const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
                    if (!Obj.IsValid()) continue;

                    const int64 TypeValue = StaticEnum<ECityEventType>()
                        ->GetValueByNameString(Obj->GetStringField(TEXT("type")));
                    if (TypeValue == INDEX_NONE) continue;

                    FCityEventDef Def;
                    Def.Type = (ECityEventType)TypeValue;
                    Obj->TryGetStringField(TEXT("scheduling"), Def.Scheduling);

                    double Number = 0.0;
                    if (Obj->TryGetNumberField(TEXT("lambda"), Number))            Def.Lambda           = (float)Number;
                    if (Obj->TryGetNumberField(TEXT("demand_multiplier"), Number)) Def.DemandMultiplier = (float)Number;
                    if (Obj->TryGetNumberField(TEXT("trigger_threshold"), Number)) Def.TriggerThreshold = (float)Number;

                    Obj->TryGetNumberField(TEXT("duration_ticks"), Def.DurationTicks);
                    Obj->TryGetBoolField(TEXT("surge_pricing_active"), Def.bSurgePricing);
                    Obj->TryGetNumberField(TEXT("demand_spike_vehicles"), Def.SpikeVehicles);
                    Obj->TryGetNumberField(TEXT("spike_window_ticks"), Def.SpikeWindowTicks);

                    FString FactionStr;
                    if (Obj->TryGetStringField(TEXT("trigger_faction"), FactionStr))
                    {
                        if (FactionStr == TEXT("police"))              Def.TriggerFaction = EFactionType::Police;
                        else if (FactionStr == TEXT("shadow_clients")) Def.TriggerFaction = EFactionType::ShadowClients;
                        else                                           Def.TriggerFaction = EFactionType::CityAuthority;
                    }

                    EventDefs.Add(MoveTemp(Def));
                }
            }
        }
        UE_LOG(LogTemp, Log, TEXT("[CitySubsystem] Loaded %d city event definitions"), EventDefs.Num());
    }
}

// ─────────────────────────────────────────────────────────────────
// TICK
// ─────────────────────────────────────────────────────────────────

void UCitySubsystem::Tick(uint64 CurrentTick)
{
    UpdateDemand(CurrentTick);
    TickEventCalendar(CurrentTick);
    TickVehicleArrivals(CurrentTick);
}

void UCitySubsystem::UpdateDemand(uint64 CurrentTick)
{
    USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>();
    if (!Sim) return;

    const int32 Hour = Sim->GetHourOfDay();
    const int32 Day  = Sim->GetCurrentDay();

    // Days 6 and 7 of each week are the weekend
    const bool bWeekend = ((Day - 1) % 7) >= 5;
    const TArray<float>& Curve = (bWeekend && WeekendCurve.Num() == 24) ? WeekendCurve : WeekdayCurve;

    float Base = 2.0f;   // fallback when JSON missing
    if (Curve.IsValidIndex(Hour))
        Base = Curve[Hour];

    // Active events multiply demand
    float Multiplier = 1.f;
    for (const FCityEventData& Event : ActiveEvents)
    {
        if (Event.bIsActive)
            Multiplier *= FMath::Max(Event.DemandMultiplier, 0.f);
    }

    CurrentLambda = Base * Multiplier;
}

void UCitySubsystem::TickEventCalendar(uint64 CurrentTick)
{
    USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>();
    if (!Sim) return;

    // ── End expired events ───────────────────────────────────────
    for (int32 i = ActiveEvents.Num() - 1; i >= 0; --i)
    {
        const FCityEventData& Event = ActiveEvents[i];
        if (CurrentTick >= Event.StartTick + (uint64)Event.DurationTicks)
            EndEvent(i, CurrentTick);
    }

    // ── Start new events ─────────────────────────────────────────
    const int32 Hour = Sim->GetHourOfDay();
    const int32 Day  = Sim->GetCurrentDay();
    const bool bHourBoundary = (CurrentTick % SubLevelSim::TicksPerHour) == 0;

    for (const FCityEventDef& Def : EventDefs)
    {
        if (HasActiveCityEvent(Def.Type)) continue;

        const uint64* Cooldown = EventCooldownUntil.Find(Def.Type);
        if (Cooldown && CurrentTick < *Cooldown) continue;

        bool bStart = false;

        if (Def.Scheduling == TEXT("stochastic"))
        {
            bStart = Sim->GetRNG().FRand() < Def.Lambda;
        }
        else if (Def.Scheduling == TEXT("scheduled") && bHourBoundary)
        {
            // Fixed calendar: stadium nights every 3rd evening, concerts every 2nd late night
            if (Def.Type == ECityEventType::StadiumNight)
                bStart = (Hour == 19) && (Day % 3 == 0);
            else if (Def.Type == ECityEventType::ConcertLetout)
                bStart = (Hour == 22) && (Day % 2 == 0);
        }
        else if (Def.Scheduling == TEXT("faction_triggered") && bHourBoundary)
        {
            const float Score = GetFactionScoreSafe(Def.TriggerFaction);
            bStart = (Def.TriggerThreshold >= 0.f)
                ? (Score >= Def.TriggerThreshold)
                : (Score <= Def.TriggerThreshold);
        }

        if (bStart)
            StartEvent(Def, CurrentTick);
    }
}

void UCitySubsystem::StartEvent(const FCityEventDef& Def, uint64 CurrentTick)
{
    FCityEventData Event;
    Event.Type             = Def.Type;
    Event.DemandMultiplier = Def.DemandMultiplier;
    Event.DurationTicks    = Def.DurationTicks;
    Event.StartTick        = CurrentTick;
    Event.bIsActive        = true;

    ActiveEvents.Add(Event);

    if (Def.Type == ECityEventType::Rainstorm)
        bRainActive = true;

    if (Def.SpikeVehicles > 0)
    {
        SpikeVehiclesRemaining = Def.SpikeVehicles;
        SpikeEndTick = CurrentTick + (uint64)FMath::Max(Def.SpikeWindowTicks, 1);
    }

    if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
        Bus->OnCityEventStarted.Broadcast(Event);

    UE_LOG(LogTemp, Log, TEXT("[CitySubsystem] City event started: %s (x%.1f demand, %d ticks)"),
        *StaticEnum<ECityEventType>()->GetNameStringByValue((int64)Def.Type),
        Def.DemandMultiplier, Def.DurationTicks);
}

void UCitySubsystem::EndEvent(int32 ActiveIndex, uint64 CurrentTick)
{
    FCityEventData Event = ActiveEvents[ActiveIndex];
    Event.bIsActive = false;
    ActiveEvents.RemoveAt(ActiveIndex);

    if (Event.Type == ECityEventType::Rainstorm)
        bRainActive = HasActiveCityEvent(ECityEventType::Rainstorm);

    // Cooldown before the same event can re-trigger (mostly for faction triggers)
    EventCooldownUntil.Add(Event.Type, CurrentTick + (uint64)Event.DurationTicks * 2);

    if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
        Bus->OnCityEventEnded.Broadcast(Event);

    UE_LOG(LogTemp, Log, TEXT("[CitySubsystem] City event ended: %s"),
        *StaticEnum<ECityEventType>()->GetNameStringByValue((int64)Event.Type));
}

// ─────────────────────────────────────────────────────────────────
// VEHICLE ARRIVALS
// ─────────────────────────────────────────────────────────────────

void UCitySubsystem::TickVehicleArrivals(uint64 CurrentTick)
{
    USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>();
    if (!Sim) return;

    // CurrentLambda is vehicles/minute → vehicles per 50ms tick
    SpawnAccumulator += CurrentLambda / 60.f * SubLevelSim::TickInterval;

    // Concert letout burst — extra arrivals over the spike window
    if (SpikeVehiclesRemaining > 0 && CurrentTick <= SpikeEndTick)
    {
        const uint64 TicksLeft = SpikeEndTick - CurrentTick + 1;
        SpikeAccumulator += (float)SpikeVehiclesRemaining / (float)TicksLeft;
        while (SpikeAccumulator >= 1.f && SpikeVehiclesRemaining > 0)
        {
            SpikeAccumulator -= 1.f;
            --SpikeVehiclesRemaining;
            SpawnAccumulator += 1.f;
        }
    }

    const float ShadowStanding = GetFactionScoreSafe(EFactionType::ShadowClients);

    while (SpawnAccumulator >= 1.f)
    {
        SpawnAccumulator -= 1.f;
        const EVehicleType Type = FVehicleTypeLoader::PickWeightedRandom(Sim->GetRNG(), ShadowStanding);
        Sim->SpawnVehicleFromCity(Type);
    }
}

// ─────────────────────────────────────────────────────────────────
// QUERIES
// ─────────────────────────────────────────────────────────────────

bool UCitySubsystem::HasActiveCityEvent(ECityEventType Type) const
{
    for (const FCityEventData& E : ActiveEvents)
    {
        if (E.Type == Type && E.bIsActive) return true;
    }
    return false;
}

bool UCitySubsystem::IsSurgePricingActive() const
{
    for (const FCityEventData& E : ActiveEvents)
    {
        if (!E.bIsActive) continue;
        for (const FCityEventDef& Def : EventDefs)
        {
            if (Def.Type == E.Type && Def.bSurgePricing) return true;
        }
    }
    return false;
}

const FCityEventDef* UCitySubsystem::FindDef(ECityEventType Type) const
{
    for (const FCityEventDef& Def : EventDefs)
    {
        if (Def.Type == Type) return &Def;
    }
    return nullptr;
}

float UCitySubsystem::GetFactionScoreSafe(EFactionType Faction) const
{
    const ASubLevelGameState* GS = GetWorld()->GetGameState<ASubLevelGameState>();
    return GS ? GS->GetFactionScore(Faction) : 0.f;
}
