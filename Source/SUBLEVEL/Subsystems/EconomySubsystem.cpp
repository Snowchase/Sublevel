#include "Subsystems/EconomySubsystem.h"
#include "Subsystems/EventBusSubsystem.h"
#include "Subsystems/SimulationSubsystem.h"
#include "Subsystems/CitySubsystem.h"
#include "Simulation/Incident/IncidentLoader.h"
#include "Simulation/Vehicle/VehicleTypeLoader.h"
#include "Staff/StaffAgent.h"
#include "Framework/SubLevelGameState.h"
#include "Engine/World.h"

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

// ─────────────────────────────────────────────────────────────────
// FEES
// ─────────────────────────────────────────────────────────────────

float UEconomySubsystem::CalculateParkingFee(float ParkedSeconds, EVehicleType Type) const
{
    if (ParkedSeconds <= 0.f) return 0.f;

    float Rate = HourlyRateBase;

    if (const FVehicleTypeDef* Def = FVehicleTypeLoader::FindDefinition(Type))
        Rate *= Def->RevenueModifier;

    if (const UCitySubsystem* City = GetWorld()->GetSubsystem<UCitySubsystem>())
    {
        if (City->IsSurgePricingActive())
            Rate *= SurgeMultiplier;
    }

    // Minimum charge: 15 minutes
    const float BilledHours = FMath::Max(ParkedSeconds, 900.f) / 3600.f;
    return BilledHours * Rate;
}

// ─────────────────────────────────────────────────────────────────
// EVENT CALLBACKS
// ─────────────────────────────────────────────────────────────────

void UEconomySubsystem::OnVehicleExited(uint32 VehicleID, float Revenue)
{
    Balance      += Revenue;
    DailyRevenue += Revenue;

    if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
        Bus->OnRevenueEarned.Broadcast(Revenue);
}

void UEconomySubsystem::OnDecisionMade(uint32 IncidentID, int32 BranchIndex)
{
    USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>();
    const FIncidentData* Incident = Sim ? Sim->FindIncident(IncidentID) : nullptr;
    if (!Incident) return;

    const FIncidentDefinition* Def = FIncidentLoader::FindDefinition(Incident->Type);
    if (!Def) return;

    ASubLevelGameState* GS = GetWorld()->GetGameState<ASubLevelGameState>();

    if (Def->Branches.IsValidIndex(BranchIndex))
    {
        const FIncidentBranch& Branch = Def->Branches[BranchIndex];

        if (GS)
        {
            // Apply each non-zero faction delta from the branch definition
            if (Branch.FactionDelta.CityAuthority != 0.f)
                GS->ApplyFactionDelta(EFactionType::CityAuthority, Branch.FactionDelta.CityAuthority);
            if (Branch.FactionDelta.Police != 0.f)
                GS->ApplyFactionDelta(EFactionType::Police, Branch.FactionDelta.Police);
            if (Branch.FactionDelta.ShadowClients != 0.f)
                GS->ApplyFactionDelta(EFactionType::ShadowClients, Branch.FactionDelta.ShadowClients);
        }

        ApplyConsequence(Branch.ConsequenceTag);
    }
    else if (GS)
    {
        // Auto-resolve type expired without handling — apply the expiry penalty
        if (Def->ExpireDelta.CityAuthority != 0.f)
            GS->ApplyFactionDelta(EFactionType::CityAuthority, Def->ExpireDelta.CityAuthority);
        if (Def->ExpireDelta.Police != 0.f)
            GS->ApplyFactionDelta(EFactionType::Police, Def->ExpireDelta.Police);
        if (Def->ExpireDelta.ShadowClients != 0.f)
            GS->ApplyFactionDelta(EFactionType::ShadowClients, Def->ExpireDelta.ShadowClients);
    }
}

void UEconomySubsystem::ApplyConsequence(const FString& Tag)
{
    // Monetary side-effects of decision branches (§10)
    float Delta = 0.f;

    if      (Tag == TEXT("vehicle_towed_revenue_recovered")) Delta =  75.f;
    else if (Tag == TEXT("fine_issued_vehicle_stays"))       Delta =  50.f;
    else if (Tag == TEXT("theft_completed_revenue_lost"))    Delta = -200.f;
    else if (Tag == TEXT("emergency_services_arrive"))       Delta = -25.f;
    else if (Tag == TEXT("vehicle_stays_shadow_credit"))     Delta =  100.f;  // shadow payoff

    if (Delta != 0.f)
    {
        Balance += Delta;
        if (Delta > 0.f) DailyRevenue += Delta;

        if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
        {
            if (Delta > 0.f) Bus->OnRevenueEarned.Broadcast(Delta);
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// TICK
// ─────────────────────────────────────────────────────────────────

void UEconomySubsystem::Tick(uint64 CurrentTick)
{
    USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>();
    if (!Sim) return;

    // ── Day boundary: wages, loan servicing, revenue reset ───────
    const int32 Day = Sim->GetCurrentDay();
    if (Day != LastProcessedDay)
        ProcessDayBoundary(Day);

    // ── Passive shadow income during BlackMarketNight ────────────
    if (const UCitySubsystem* City = GetWorld()->GetSubsystem<UCitySubsystem>())
    {
        if (City->HasActiveCityEvent(ECityEventType::BlackMarketNight) &&
            !City->HasActiveCityEvent(ECityEventType::PoliceSweep))
        {
            Balance      += ShadowIncomePerTick;
            DailyRevenue += ShadowIncomePerTick;
        }
    }

    MirrorToGameState();
}

void UEconomySubsystem::ProcessDayBoundary(int32 NewDay)
{
    USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>();
    UEventBusSubsystem*   Bus = UEventBusSubsystem::Get(this);

    // ── Staff wages ──────────────────────────────────────────────
    float Wages = 0.f;
    if (Sim)
    {
        for (const auto& [ID, Agent] : Sim->GetStaffActors())
        {
            if (Agent) Wages += StaffDailyWage(Agent->GetStaffData().Role);
        }
    }
    Balance -= Wages;

    // ── Loan servicing ───────────────────────────────────────────
    if (LoanPrincipal > 0.f)
    {
        const float Interest  = LoanPrincipal * LoanInterestRate;
        const float Principal = FMath::Min(LoanPrincipal, LoanPrincipal * LoanRepaymentFraction);
        const float Due       = Interest + Principal;

        Balance       -= Due;
        LoanPrincipal -= Principal;
        if (LoanPrincipal < 1.f) LoanPrincipal = 0.f;

        if (Bus) Bus->OnLoanRepaymentDue.Broadcast(Due);
    }

    // ── Default check ────────────────────────────────────────────
    if (Balance < DefaultThreshold && !bLoanDefaulted)
    {
        bLoanDefaulted = true;
        if (Bus) Bus->OnLoanDefaulted.Broadcast();
        UE_LOG(LogTemp, Warning, TEXT("[Economy] LOAN DEFAULT — balance %.0f"), Balance);
    }

    UE_LOG(LogTemp, Log, TEXT("[Economy] Day %d closed. Revenue: %.2f, Wages: %.2f, Balance: %.2f"),
        LastProcessedDay, DailyRevenue, Wages, Balance);

    DailyRevenue = 0.f;
    LastProcessedDay = NewDay;
}

float UEconomySubsystem::StaffDailyWage(EStaffRole Role) const
{
    switch (Role)
    {
        case EStaffRole::Attendant:   return 80.f;
        case EStaffRole::Security:    return 120.f;
        case EStaffRole::Maintenance: return 100.f;
        default:                      return 80.f;
    }
}

// ─────────────────────────────────────────────────────────────────
// LOANS / STATE
// ─────────────────────────────────────────────────────────────────

void UEconomySubsystem::TakeLoan(float Amount)
{
    if (Amount <= 0.f) return;
    Balance       += Amount;
    LoanPrincipal += Amount;
    UE_LOG(LogTemp, Log, TEXT("[Economy] Loan taken: %.0f (principal now %.0f)"), Amount, LoanPrincipal);
}

void UEconomySubsystem::SetEconomyState(float InBalance, float InLoanPrincipal, float InDailyRevenue)
{
    Balance        = InBalance;
    LoanPrincipal  = InLoanPrincipal;
    DailyRevenue   = InDailyRevenue;
    bLoanDefaulted = false;

    if (USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>())
        LastProcessedDay = Sim->GetCurrentDay();

    MirrorToGameState();
}

void UEconomySubsystem::MirrorToGameState()
{
    ASubLevelGameState* GS = GetWorld()->GetGameState<ASubLevelGameState>();
    if (!GS) return;

    GS->CurrentBalance   = Balance;
    GS->DailyRevenue     = DailyRevenue;
    GS->LoanPrincipal    = LoanPrincipal;
    GS->LoanRepaymentDue = LoanPrincipal * (LoanInterestRate + LoanRepaymentFraction);

    if (const USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>())
        GS->CurrentDay = Sim->GetCurrentDay();
}
