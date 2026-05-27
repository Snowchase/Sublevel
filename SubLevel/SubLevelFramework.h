// ═════════════════════════════════════════════════════════════════
// SubLevelGameMode.h
// Session init, RNG seed creation, win/fail conditions
// ═════════════════════════════════════════════════════════════════
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SubLevelGameMode.generated.h"

UCLASS()
class SUBLEVEL_API ASubLevelGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ASubLevelGameMode();

    virtual void InitGame(
        const FString& MapName,
        const FString& Options,
        FString&       ErrorMessage) override;

    virtual void StartPlay() override;

    // Called by save system when loading an existing session
    void LoadSessionFromSeed(int32 SavedSeed, uint64 SavedTick, int32 SavedDay);

    // Exposed so Python tooling can force a seed in editor (via command line arg)
    UPROPERTY(EditAnywhere, Category = "Debug")
    int32 DebugForcedSeed = -1;     // -1 = generate random seed

private:
    int32 GenerateSeed() const;
    void  InitializeSubsystems(int32 Seed);

    // In-game time scale: 1.0 = real-time, 60.0 = 1 real-sec = 1 in-game-min
    UPROPERTY(EditAnywhere, Category = "Simulation")
    float TimeScaleMultiplier = 20.0f;
};


// ═════════════════════════════════════════════════════════════════
// SubLevelGameState.h
// Authoritative shared state — floors, faction scores, time
// ═════════════════════════════════════════════════════════════════
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "SubLevelTypes.h"
#include "SubLevelGameState.generated.h"

UCLASS()
class SUBLEVEL_API ASubLevelGameState : public AGameStateBase
{
    GENERATED_BODY()

public:

    // ── Floor state ───────────────────────────────────────────────
    UFUNCTION(BlueprintCallable) bool  IsFloorUnlocked(int32 FloorIndex) const;
    UFUNCTION(BlueprintCallable) float GetFloorIntegrity(int32 FloorIndex) const;
    void SetFloorUnlocked(int32 FloorIndex, bool bUnlocked);
    void SetFloorIntegrity(int32 FloorIndex, float Integrity);

    // ── Faction ───────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable) float GetFactionScore(EFactionType Faction) const;
    void ApplyFactionDelta(EFactionType Faction, float Delta);

    // ── Economy snapshot (read by UI) ─────────────────────────────
    UPROPERTY(BlueprintReadOnly) float  CurrentBalance     = 0.0f;
    UPROPERTY(BlueprintReadOnly) float  DailyRevenue       = 0.0f;
    UPROPERTY(BlueprintReadOnly) float  LoanPrincipal      = 0.0f;
    UPROPERTY(BlueprintReadOnly) float  LoanRepaymentDue   = 0.0f;

    // ── Session ───────────────────────────────────────────────────
    UPROPERTY(BlueprintReadOnly) int32  CurrentDay         = 1;
    UPROPERTY(BlueprintReadOnly) int32  ActiveSeed         = 0;

private:
    TArray<bool>  FloorUnlocked;     // Index = FloorIndex; B1=0 pre-unlocked
    TArray<float> FloorIntegrity;    // [0,1] per floor
    FFactionScores FactionScores;
};


// ═════════════════════════════════════════════════════════════════
// SubLevelPlayerController.h
// Input, camera rig (90° ortho), UI command routing
// ═════════════════════════════════════════════════════════════════
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "SubLevelPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;

UENUM(BlueprintType)
enum class EPlayerMode : uint8
{
    Management,     // Default — panning, floor switch, incident response
    BuildMode,      // Tile placement active
    CCTVMode        // Tab-mode camera feed view
};

UCLASS()
class SUBLEVEL_API ASubLevelPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    ASubLevelPlayerController();

    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;

    // ── Mode switching ────────────────────────────────────────────
    UFUNCTION(BlueprintCallable) void SetPlayerMode(EPlayerMode NewMode);
    UFUNCTION(BlueprintCallable) EPlayerMode GetPlayerMode() const { return CurrentMode; }

    // ── Floor switching ───────────────────────────────────────────
    UFUNCTION(BlueprintCallable) void SwitchToFloor(int32 FloorIndex);
    UFUNCTION(BlueprintCallable) int32 GetActiveFloor() const { return ActiveFloorIndex; }

    // ── Camera ────────────────────────────────────────────────────
    // 90° orthographic top-down. Zoom via ortho width.
    void SetCameraZoom(float NewOrthoWidth);
    void PanCamera(FVector2D Delta);

    // ── CCTV ──────────────────────────────────────────────────────
    void CycleNextCamera();
    void CyclePrevCamera();

private:

    // ── Input bindings (Enhanced Input) ──────────────────────────
    UPROPERTY(EditAnywhere, Category = "Input")
    UInputMappingContext* IMC_Management = nullptr;

    UPROPERTY(EditAnywhere, Category = "Input")
    UInputMappingContext* IMC_BuildMode = nullptr;

    UPROPERTY(EditAnywhere, Category = "Input")
    UInputMappingContext* IMC_CCTV = nullptr;

    UPROPERTY(EditAnywhere, Category = "Input")
    UInputAction* IA_Pan = nullptr;

    UPROPERTY(EditAnywhere, Category = "Input")
    UInputAction* IA_Zoom = nullptr;

    UPROPERTY(EditAnywhere, Category = "Input")
    UInputAction* IA_ToggleBuildMode = nullptr;

    UPROPERTY(EditAnywhere, Category = "Input")
    UInputAction* IA_ToggleCCTV = nullptr;

    UPROPERTY(EditAnywhere, Category = "Input")
    UInputAction* IA_FloorUp = nullptr;

    UPROPERTY(EditAnywhere, Category = "Input")
    UInputAction* IA_FloorDown = nullptr;

    // ── Input callbacks ───────────────────────────────────────────
    void OnPan(const FInputActionValue& Value);
    void OnZoom(const FInputActionValue& Value);
    void OnToggleBuildMode();
    void OnToggleCCTV();
    void OnFloorUp();
    void OnFloorDown();

    // ── State ─────────────────────────────────────────────────────
    EPlayerMode CurrentMode      = EPlayerMode::Management;
    int32       ActiveFloorIndex = 0;   // B1 = 0
    int32       ActiveCameraIdx  = 0;   // CCTV mode current camera

    // Camera rig
    UPROPERTY() class ACameraActor* TopDownCamera = nullptr;

    float MinOrthoWidth  =  800.0f;
    float MaxOrthoWidth  = 6000.0f;
    float CurrentOrthoWidth = 3000.0f;
};
