#include "Framework/SubLevelFramework.h"
#include "Framework/SubLevelCameraPawn.h"
#include "UI/SubLevelHUD.h"
#include "Subsystems/SimulationSubsystem.h"
#include "Subsystems/EventBusSubsystem.h"
#include "Simulation/Incident/IncidentLoader.h"
#include "Staff/StaffAgent.h"
#include "Persistence/SaveSystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/DateTime.h"

// ─────────────────────────────────────────────────────────────────
// GAME MODE
// ─────────────────────────────────────────────────────────────────

ASubLevelGameMode::ASubLevelGameMode()
{
    GameStateClass        = ASubLevelGameState::StaticClass();
    PlayerControllerClass = ASubLevelPlayerController::StaticClass();
    DefaultPawnClass      = ASubLevelCameraPawn::StaticClass();
    HUDClass              = ASubLevelHUD::StaticClass();
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

    // Test-build convenience: a starter crew so incidents get handled.
    if (USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>())
    {
        if (Sim->GetStaffCount() == 0 && Sim->GetFloor(0))
        {
            Sim->HireStaff(EStaffRole::Attendant,   0);
            Sim->HireStaff(EStaffRole::Security,    0);
            Sim->HireStaff(EStaffRole::Maintenance, 0);
        }
    }

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
    // Camera pawn is acquired lazily in PlayerTick — possession order during
    // login varies, so BeginPlay is too early to rely on GetPawn().
}

void ASubLevelPlayerController::EnsureCameraPawn()
{
    CameraPawn = Cast<ASubLevelCameraPawn>(GetPawn());
    if (CameraPawn) return;

    // No PlayerStart in the level → RestartPlayer never spawned a pawn.
    // Spawn the camera rig over the center of the default test floor.
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    CameraPawn = GetWorld()->SpawnActor<ASubLevelCameraPawn>(
        ASubLevelCameraPawn::StaticClass(),
        FVector(1200.f, 800.f, CurrentOrthoWidth),
        FRotator::ZeroRotator,
        Params);

    if (CameraPawn)
        Possess(CameraPawn);
}

void ASubLevelPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    // Test-build raw key bindings. Enhanced Input contexts replace these
    // once IMC assets are authored in the editor (§13).
    if (!InputComponent) return;

    InputComponent->BindKey(EKeys::MouseScrollUp,   IE_Pressed, this, &ASubLevelPlayerController::OnZoomIn);
    InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ASubLevelPlayerController::OnZoomOut);

    InputComponent->BindKey(EKeys::One,   IE_Pressed, this, &ASubLevelPlayerController::OnDecision1);
    InputComponent->BindKey(EKeys::Two,   IE_Pressed, this, &ASubLevelPlayerController::OnDecision2);
    InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ASubLevelPlayerController::OnDecision3);

    InputComponent->BindKey(EKeys::F1, IE_Pressed, this, &ASubLevelPlayerController::OnHireAttendant);
    InputComponent->BindKey(EKeys::F2, IE_Pressed, this, &ASubLevelPlayerController::OnHireSecurity);
    InputComponent->BindKey(EKeys::F3, IE_Pressed, this, &ASubLevelPlayerController::OnHireMaintenance);

    InputComponent->BindKey(EKeys::F5, IE_Pressed, this, &ASubLevelPlayerController::OnSaveGame);
    InputComponent->BindKey(EKeys::F9, IE_Pressed, this, &ASubLevelPlayerController::OnLoadGame);

    InputComponent->BindKey(EKeys::PageUp,   IE_Pressed, this, &ASubLevelPlayerController::OnFloorUp);
    InputComponent->BindKey(EKeys::PageDown, IE_Pressed, this, &ASubLevelPlayerController::OnFloorDown);
}

void ASubLevelPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);

    // Acquire (or spawn) the camera pawn once possession has settled.
    if (!CameraPawn || GetPawn() != CameraPawn)
    {
        EnsureCameraPawn();
        if (CameraPawn)
            SetCameraZoom(CurrentOrthoWidth);
    }

    if (!CameraPawn) return;

    // Held-key panning — screen up is world +X with a -90° pitch camera.
    FVector2D Pan = FVector2D::ZeroVector;
    if (IsInputKeyDown(EKeys::W) || IsInputKeyDown(EKeys::Up))    Pan.X += 1.f;
    if (IsInputKeyDown(EKeys::S) || IsInputKeyDown(EKeys::Down))  Pan.X -= 1.f;
    if (IsInputKeyDown(EKeys::D) || IsInputKeyDown(EKeys::Right)) Pan.Y += 1.f;
    if (IsInputKeyDown(EKeys::A) || IsInputKeyDown(EKeys::Left))  Pan.Y -= 1.f;

    if (!Pan.IsNearlyZero())
    {
        const float Speed = CurrentOrthoWidth * 0.8f;   // pan faster when zoomed out
        CameraPawn->AddPanInput(Pan.GetSafeNormal() * Speed * DeltaTime);
    }
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
    if (CameraPawn)
        CameraPawn->SetCameraHeight(CurrentOrthoWidth);
}

void ASubLevelPlayerController::PanCamera(FVector2D Delta)
{
    if (CameraPawn)
        CameraPawn->AddPanInput(Delta);
}

void ASubLevelPlayerController::OnZoomIn()  { SetCameraZoom(CurrentOrthoWidth - 300.f); }
void ASubLevelPlayerController::OnZoomOut() { SetCameraZoom(CurrentOrthoWidth + 300.f); }

void ASubLevelPlayerController::OnDecision1() { ResolveDecisionBranch(0); }
void ASubLevelPlayerController::OnDecision2() { ResolveDecisionBranch(1); }
void ASubLevelPlayerController::OnDecision3() { ResolveDecisionBranch(2); }

void ASubLevelPlayerController::ResolveDecisionBranch(int32 BranchIndex)
{
    USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>();
    if (!Sim) return;

    const uint32 IncidentID = Sim->GetFirstPendingDecisionID();
    if (IncidentID == 0) return;

    const FIncidentData* Incident = Sim->FindIncident(IncidentID);
    const FIncidentDefinition* Def = Incident ? FIncidentLoader::FindDefinition(Incident->Type) : nullptr;
    if (!Def || !Def->Branches.IsValidIndex(BranchIndex)) return;

    Sim->ResolveIncident(IncidentID, BranchIndex);

    if (GEngine)
        GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
            FString::Printf(TEXT("Decision: %s"), *Def->Branches[BranchIndex].Label));
}

void ASubLevelPlayerController::OnHireAttendant()   { HireStaffOfRole(EStaffRole::Attendant); }
void ASubLevelPlayerController::OnHireSecurity()    { HireStaffOfRole(EStaffRole::Security); }
void ASubLevelPlayerController::OnHireMaintenance() { HireStaffOfRole(EStaffRole::Maintenance); }

void ASubLevelPlayerController::HireStaffOfRole(EStaffRole Role)
{
    if (USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>())
    {
        Sim->HireStaff(Role, ActiveFloorIndex);
        if (GEngine)
            GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan,
                FString::Printf(TEXT("Hired %s"),
                    *StaticEnum<EStaffRole>()->GetNameStringByValue((int64)Role)));
    }
}

void ASubLevelPlayerController::OnSaveGame()
{
    const bool bOk = FSaveSystem::SaveToFile(TEXT("slot1"), GetWorld());
    if (GEngine)
        GEngine->AddOnScreenDebugMessage(-1, 3.f, bOk ? FColor::Green : FColor::Red,
            bOk ? TEXT("Saved to slot1") : TEXT("Save FAILED"));
}

void ASubLevelPlayerController::OnLoadGame()
{
    const bool bOk = FSaveSystem::LoadFromFile(TEXT("slot1"), GetWorld());
    if (GEngine)
        GEngine->AddOnScreenDebugMessage(-1, 3.f, bOk ? FColor::Green : FColor::Red,
            bOk ? TEXT("Loaded slot1") : TEXT("Load FAILED (no save?)"));
}

void ASubLevelPlayerController::CycleNextCamera()  {}
void ASubLevelPlayerController::CyclePrevCamera()  {}
void ASubLevelPlayerController::OnPan(const FInputActionValue&) {}
void ASubLevelPlayerController::OnZoom(const FInputActionValue&) {}
void ASubLevelPlayerController::OnToggleBuildMode() {}
void ASubLevelPlayerController::OnToggleCCTV()      {}
void ASubLevelPlayerController::OnFloorUp()         { SwitchToFloor(ActiveFloorIndex + 1); }
void ASubLevelPlayerController::OnFloorDown()       { SwitchToFloor(FMath::Max(0, ActiveFloorIndex - 1)); }


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
