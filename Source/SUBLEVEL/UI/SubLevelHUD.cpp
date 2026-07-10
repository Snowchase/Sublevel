#include "UI/SubLevelHUD.h"
#include "Subsystems/SimulationSubsystem.h"
#include "Subsystems/EconomySubsystem.h"
#include "Subsystems/CitySubsystem.h"
#include "Simulation/Incident/IncidentLoader.h"
#include "Framework/SubLevelGameState.h"
#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"

namespace
{
    const FLinearColor ColHeader  (1.0f, 0.85f, 0.3f);
    const FLinearColor ColText    (0.9f, 0.9f, 0.9f);
    const FLinearColor ColGood    (0.4f, 0.9f, 0.4f);
    const FLinearColor ColBad     (1.0f, 0.4f, 0.35f);
    const FLinearColor ColWarn    (1.0f, 0.7f, 0.2f);
    const FLinearColor ColMuted   (0.6f, 0.6f, 0.65f);

    FString EnumName(const UEnum* Enum, int64 Value)
    {
        return Enum ? Enum->GetNameStringByValue(Value) : TEXT("?");
    }
}

void ASubLevelHUD::Line(const FString& Text, const FLinearColor& Color, float X, float& Y, float Scale)
{
    DrawText(Text, Color, X, Y, GEngine->GetSmallFont(), Scale);
    Y += 16.f * Scale;
}

void ASubLevelHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !GetWorld()) return;

    USimulationSubsystem* Sim     = GetWorld()->GetSubsystem<USimulationSubsystem>();
    UEconomySubsystem*    Economy = GetWorld()->GetSubsystem<UEconomySubsystem>();
    UCitySubsystem*       City    = GetWorld()->GetSubsystem<UCitySubsystem>();
    ASubLevelGameState*   GS      = GetWorld()->GetGameState<ASubLevelGameState>();

    float Y = 20.f;
    const float X = 20.f;

    if (!Sim)
    {
        Line(TEXT("SUBLEVEL — SimulationSubsystem not running"), ColBad, X, Y);
        return;
    }

    // ── Session ──────────────────────────────────────────────────
    Line(TEXT("SUBLEVEL — test build"), ColHeader, X, Y, 1.2f);
    Line(FString::Printf(TEXT("Day %d   %02d:00   tick %llu   seed %d"),
        Sim->GetCurrentDay(), Sim->GetHourOfDay(), Sim->GetCurrentTick(), Sim->GetCurrentSeed()),
        ColText, X, Y);
    Y += 6.f;

    // ── Economy ──────────────────────────────────────────────────
    if (Economy)
    {
        const float Balance = Economy->GetCurrentBalance();
        Line(FString::Printf(TEXT("Balance $%.0f   today $%.0f   loan $%.0f"),
            Balance, Economy->GetDailyRevenue(), Economy->GetLoanPrincipal()),
            Balance >= 0.f ? ColGood : ColBad, X, Y);
    }

    // ── Factions ─────────────────────────────────────────────────
    if (GS)
    {
        Line(FString::Printf(TEXT("City %+.0f   Police %+.0f   Shadow %+.0f"),
            GS->GetFactionScore(EFactionType::CityAuthority),
            GS->GetFactionScore(EFactionType::Police),
            GS->GetFactionScore(EFactionType::ShadowClients)),
            ColText, X, Y);
    }

    // ── City ─────────────────────────────────────────────────────
    if (City)
    {
        FString EventList;
        for (const FCityEventData& Event : City->GetActiveEvents())
        {
            if (!Event.bIsActive) continue;
            if (!EventList.IsEmpty()) EventList += TEXT(", ");
            EventList += EnumName(StaticEnum<ECityEventType>(), (int64)Event.Type);
        }
        if (EventList.IsEmpty()) EventList = TEXT("none");

        Line(FString::Printf(TEXT("Demand %.1f veh/min%s   events: %s"),
            City->GetCurrentDemandLambda(),
            City->IsRainActive() ? TEXT("  [RAIN]") : TEXT(""),
            *EventList),
            ColMuted, X, Y);
    }

    Line(FString::Printf(TEXT("Vehicles %d   Staff %d"),
        Sim->GetVehicleCount(), Sim->GetStaffCount()), ColMuted, X, Y);
    Y += 10.f;

    // ── Incident alerts ──────────────────────────────────────────
    const UEnum* IncidentEnum = StaticEnum<EIncidentType>();
    int32 Shown = 0;

    for (const auto& [ID, Incident] : Sim->GetIncidents())
    {
        if (Incident.State == EIncidentState::Resolved ||
            Incident.State == EIncidentState::Expired ||
            Incident.State == EIncidentState::Pending)
            continue;
        if (Shown >= 8) { Line(TEXT("..."), ColMuted, X, Y); break; }

        FLinearColor Color = ColWarn;
        FString StateTag;
        switch (Incident.State)
        {
            case EIncidentState::DecisionPending: Color = ColBad;  StateTag = TEXT("DECIDE"); break;
            case EIncidentState::InProgress:      Color = ColGood; StateTag = TEXT("staff");  break;
            default:                              Color = ColWarn; StateTag = TEXT("active"); break;
        }

        Line(FString::Printf(TEXT("[%s] %s  floor %d  sev %.1f"),
            *StateTag,
            *EnumName(IncidentEnum, (int64)Incident.Type),
            Incident.FloorIndex,
            Incident.Severity),
            Color, X, Y);
        ++Shown;
    }

    // ── Decision card ────────────────────────────────────────────
    const uint32 DecisionID = Sim->GetFirstPendingDecisionID();
    if (DecisionID != 0)
    {
        const FIncidentData* Incident = Sim->FindIncident(DecisionID);
        const FIncidentDefinition* Def =
            Incident ? FIncidentLoader::FindDefinition(Incident->Type) : nullptr;

        if (Incident && Def)
        {
            const float CenterX = Canvas->SizeX * 0.5f - 180.f;
            float CardY = Canvas->SizeY * 0.62f;

            const float SecondsLeft = Incident->DecisionDeadlineTick > Sim->GetCurrentTick()
                ? (Incident->DecisionDeadlineTick - Sim->GetCurrentTick()) * 0.05f
                : 0.f;

            Line(FString::Printf(TEXT("DECISION — %s   (%.0fs left)"),
                *EnumName(IncidentEnum, (int64)Incident->Type), SecondsLeft),
                ColBad, CenterX, CardY, 1.3f);

            for (int32 i = 0; i < Def->Branches.Num() && i < 3; ++i)
            {
                Line(FString::Printf(TEXT("  [%d] %s"), i + 1, *Def->Branches[i].Label),
                    ColText, CenterX, CardY, 1.15f);
            }
        }
    }

    // ── Controls footer ──────────────────────────────────────────
    float FooterY = Canvas->SizeY - 40.f;
    Line(TEXT("WASD/arrows pan   wheel zoom   1-3 decide   F1/F2/F3 hire attendant/security/maintenance   F5 save   F9 load"),
        ColMuted, X, FooterY);
}
