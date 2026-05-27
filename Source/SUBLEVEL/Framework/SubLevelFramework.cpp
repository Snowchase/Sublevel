#include "Framework/SubLevelGameMode.h"
#include "Framework/SubLevelGameState.h"
#include "Framework/SubLevelPlayerController.h"
#include "Subsystems/SimulationSubsystem.h"
#include "Subsystems/EventBusSubsystem.h"
#include "Engine/World.h"
#include "Misc/DateTime.h"

// ─────────────────────────────────────────────────────────────────
// GAME MODE
// ─────────────────────────────────────────────────────────────────

ASubLevelGameMode::ASubLevelGameMode()
{
    GameStateClass        = ASubLevelGameState::StaticClass();
    PlayerControllerClass = ASubLevelPlayerController::StaticClass();
}

void ASubLevelGameMode::InitGame(
    const FString& MapName,
    const FString& Options,
    FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);

    const int32 Seed = (DebugForcedSeed >= 0) ? DebugForcedSeed : GenerateSeed();
    InitializeSubsystems(Seed);
}

void ASubLevelGameMode::StartPlay()
{
    Super::StartPlay();
    UE_LOG(LogTemp, Log, TEXT("[GameMode] Session started. Day 1."));
}

void ASubLevelGameMode::LoadSessionFromSeed(int32 SavedSeed, uint64 SavedTick, int32 SavedDay)
{
    InitializeSubsystems(SavedSeed);
    UE_LOG(LogTemp, Log, TEXT("[GameMode] Loaded session. Seed=%d, Tick=%llu, Day=%d"),
        SavedSeed, SavedTick, SavedDay);
}

int32 ASubLevelGameMode::GenerateSeed() const
{
    return (int32)(FDateTime::UtcNow().GetTicks() & 0x7FFFFFFF);
}

void ASubLevelGameMode::InitializeSubsystems(int32 Seed)
{
    if (USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>())
        Sim->InitializeRNG(Seed);

    if (ASubLevelGameState* GS = GetGameState<ASubLevelGameState>())
        GS->ActiveSeed = Seed;

    UE_LOG(LogTemp, Log, TEXT("[GameMode] Subsystems initialized with seed: %d"), Seed);
}


// ─────────────────────────────────────────────────────────────────
// GAME STATE
// ─────────────────────────────────────────────────────────────────

ASubLevelGameState::ASubLevelGameState()
{
    constexpr int32 NumFloors = 5;
    FloorUnlocked.Init(false, NumFloors);
    FloorUnlocked[0] = true;
    FloorIntegrity.Init(1.0f, NumFloors);
}

bool ASubLevelGameState::IsFloorUnlocked(int32 FloorIndex) const
{
    return FloorUnlocked.IsValidIndex(FloorIndex) && FloorUnlocked[FloorIndex];
}

float ASubLevelGameState::GetFloorIntegrity(int32 FloorIndex) const
{
    return FloorIntegrity.IsValidIndex(FloorIndex) ? FloorIntegrity[FloorIndex] : 1.0f;
}

void ASubLevelGameState::SetFloorUnlocked(int32 FloorIndex, bool bUnlocked)
{
    if (FloorUnlocked.IsValidIndex(FloorIndex))
        FloorUnlocked[FloorIndex] = bUnlocked;
}

void ASubLevelGameState::SetFloorIntegrity(int32 FloorIndex, float Integrity)
{
    if (FloorIntegrity.IsValidIndex(FloorIndex))
        FloorIntegrity[FloorIndex] = FMath::Clamp(Integrity, 0.0f, 1.0f);
}

float ASubLevelGameState::GetFactionScore(EFactionType Faction) const
{
    return FactionScores.Get(Faction);
}

void ASubLevelGameState::ApplyFactionDelta(EFactionType Faction, float Delta)
{
    FactionScores.Apply(Faction, Delta);

    if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
        Bus->OnFactionChanged.Broadcast(Faction, FactionScores.Get(Faction));
}


// ─────────────────────────────────────────────────────────────────
// PLAYER CONTROLLER
// ─────────────────────────────────────────────────────────────────

ASubLevelPlayerController::ASubLevelPlayerController()
{
    bShowMouseCursor      = true;
    bEnableClickEvents    = true;
    bEnableMouseOverEvents = true;
}

void ASubLevelPlayerController::BeginPlay()
{
    Super::BeginPlay();
    SetCameraZoom(CurrentOrthoWidth);
}

void ASubLevelPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    // Enhanced Input contexts bound here once IMC assets are assigned in editor.
}

void ASubLevelPlayerController::SetPlayerMode(EPlayerMode NewMode)
{
    CurrentMode = NewMode;
    // Swap Enhanced Input Mapping Contexts via UEnhancedInputLocalPlayerSubsystem.
}

void ASubLevelPlayerController::SwitchToFloor(int32 FloorIndex)
{
    ActiveFloorIndex = FloorIndex;
}

void ASubLevelPlayerController::SetCameraZoom(float NewOrthoWidth)
{
    CurrentOrthoWidth = FMath::Clamp(NewOrthoWidth, MinOrthoWidth, MaxOrthoWidth);
}

void ASubLevelPlayerController::PanCamera(FVector2D Delta)
{
    // Move TopDownCamera in XY — clamped to floor bounds.
}

void ASubLevelPlayerController::CycleNextCamera()  {}
void ASubLevelPlayerController::CyclePrevCamera()  {}
void ASubLevelPlayerController::OnPan(const FInputActionValue&) {}
void ASubLevelPlayerController::OnZoom(const FInputActionValue&) {}
void ASubLevelPlayerController::OnToggleBuildMode() {}
void ASubLevelPlayerController::OnToggleCCTV()      {}
void ASubLevelPlayerController::OnFloorUp()         {}
void ASubLevelPlayerController::OnFloorDown()       {}


// ─────────────────────────────────────────────────────────────────
// FACTION SCORE HELPERS
// ─────────────────────────────────────────────────────────────────

float FFactionScores::Get(EFactionType Faction) const
{
    switch (Faction)
    {
        case EFactionType::CityAuthority: return CityAuthority;
        case EFactionType::Police:        return Police;
        case EFactionType::ShadowClients: return ShadowClients;
        default: return 0.0f;
    }
}

void FFactionScores::Apply(EFactionType Faction, float Delta)
{
    switch (Faction)
    {
        case EFactionType::CityAuthority:
            CityAuthority = FMath::Clamp(CityAuthority + Delta, -100.0f, 100.0f); break;
        case EFactionType::Police:
            Police        = FMath::Clamp(Police        + Delta, -100.0f, 100.0f); break;
        case EFactionType::ShadowClients:
            ShadowClients = FMath::Clamp(ShadowClients + Delta, -100.0f, 100.0f); break;
    }
}
