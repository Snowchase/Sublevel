#include "Framework/SubLevelFramework.h"
#include "Subsystems/SimulationSubsystem.h"
#include "Subsystems/EventBusSubsystem.h"
#include "Engine/World.h"
#include "Misc/DateTime.h"

// ─────────────────────────────────────────────────────────────────
// GAME MODE
// ─────────────────────────────────────────────────────────────────

ASubLevelGameMode::ASubLevelGameMode()
{
    // Set default classes — assign in Blueprint subclass or here directly
    // GameStateClass       = ASubLevelGameState::StaticClass();
    // PlayerControllerClass = ASubLevelPlayerController::StaticClass();
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
    // SaveSystem will restore full state after this call
    UE_LOG(LogTemp, Log, TEXT("[GameMode] Loaded session. Seed=%d, Tick=%llu, Day=%d"),
        SavedSeed, SavedTick, SavedDay);
}

int32 ASubLevelGameMode::GenerateSeed() const
{
    // Use current timestamp as entropy source
    return (int32)(FDateTime::UtcNow().GetTicks() & 0x7FFFFFFF);
}

void ASubLevelGameMode::InitializeSubsystems(int32 Seed)
{
    USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>();
    if (Sim)
    {
        Sim->InitializeRNG(Seed);
    }

    if (ASubLevelGameState* GS = GetGameState<ASubLevelGameState>())
    {
        GS->ActiveSeed = Seed;
    }

    UE_LOG(LogTemp, Log, TEXT("[GameMode] Subsystems initialized with seed: %d"), Seed);
}


// ─────────────────────────────────────────────────────────────────
// GAME STATE
// ─────────────────────────────────────────────────────────────────

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

    // Notify UI and city system via EventBus
    if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
    {
        Bus->OnFactionChanged.Broadcast(Faction, FactionScores.Get(Faction));
    }
}


// ─────────────────────────────────────────────────────────────────
// PLAYER CONTROLLER
// ─────────────────────────────────────────────────────────────────

ASubLevelPlayerController::ASubLevelPlayerController()
{
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;
}

void ASubLevelPlayerController::BeginPlay()
{
    Super::BeginPlay();

    // Set up orthographic camera at 90°
    // Camera actor placed in level looking straight down (-Z)
    // OrthoWidth drives zoom level
    SetCameraZoom(CurrentOrthoWidth);
}

void ASubLevelPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    // Enhanced Input bindings set up here
    // Assign IMC_Management as default context
}

void ASubLevelPlayerController::SetPlayerMode(EPlayerMode NewMode)
{
    CurrentMode = NewMode;

    // Swap Enhanced Input Mapping Contexts
    // IMC_Management / IMC_BuildMode / IMC_CCTV
    // Implementation: use GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()
}

void ASubLevelPlayerController::SwitchToFloor(int32 FloorIndex)
{
    ActiveFloorIndex = FloorIndex;
    // Notify HUD to update floor switcher highlight
    // Pan camera to floor center (floors are vertically stacked in world space)
}

void ASubLevelPlayerController::SetCameraZoom(float NewOrthoWidth)
{
    CurrentOrthoWidth = FMath::Clamp(NewOrthoWidth, MinOrthoWidth, MaxOrthoWidth);
    // Apply to TopDownCamera->GetCameraComponent()->OrthoWidth
}

void ASubLevelPlayerController::PanCamera(FVector2D Delta)
{
    // Move TopDownCamera actor in XY plane — clamped to floor bounds
}

void ASubLevelPlayerController::CycleNextCamera()
{
    // Advance ActiveCameraIdx, wrap around
    // Pull camera feed texture from ASecurityCamera at new index
}

void ASubLevelPlayerController::CyclePrevCamera()
{
    // Decrement ActiveCameraIdx, wrap around
}


// ─────────────────────────────────────────────────────────────────
// FACTION SCORE HELPERS (SubLevelTypes.cpp equivalent)
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
            CityAuthority  = FMath::Clamp(CityAuthority  + Delta, -100.0f, 100.0f); break;
        case EFactionType::Police:
            Police         = FMath::Clamp(Police         + Delta, -100.0f, 100.0f); break;
        case EFactionType::ShadowClients:
            ShadowClients  = FMath::Clamp(ShadowClients  + Delta, -100.0f, 100.0f); break;
    }
}
