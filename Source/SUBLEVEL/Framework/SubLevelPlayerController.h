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
    Management,
    BuildMode,
    CCTVMode
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
    UFUNCTION(BlueprintCallable) void       SetPlayerMode(EPlayerMode NewMode);
    UFUNCTION(BlueprintCallable) EPlayerMode GetPlayerMode() const { return CurrentMode; }

    // ── Floor switching ───────────────────────────────────────────
    UFUNCTION(BlueprintCallable) void  SwitchToFloor(int32 FloorIndex);
    UFUNCTION(BlueprintCallable) int32 GetActiveFloor() const { return ActiveFloorIndex; }

    // ── Camera ────────────────────────────────────────────────────
    void SetCameraZoom(float NewOrthoWidth);
    void PanCamera(FVector2D Delta);

    // ── CCTV ──────────────────────────────────────────────────────
    void CycleNextCamera();
    void CyclePrevCamera();

private:
    // ── Enhanced Input assets (assign in editor or Blueprint subclass) ──
    UPROPERTY(EditAnywhere, Category = "Input") UInputMappingContext* IMC_Management    = nullptr;
    UPROPERTY(EditAnywhere, Category = "Input") UInputMappingContext* IMC_BuildMode     = nullptr;
    UPROPERTY(EditAnywhere, Category = "Input") UInputMappingContext* IMC_CCTV          = nullptr;
    UPROPERTY(EditAnywhere, Category = "Input") UInputAction*         IA_Pan            = nullptr;
    UPROPERTY(EditAnywhere, Category = "Input") UInputAction*         IA_Zoom           = nullptr;
    UPROPERTY(EditAnywhere, Category = "Input") UInputAction*         IA_ToggleBuildMode = nullptr;
    UPROPERTY(EditAnywhere, Category = "Input") UInputAction*         IA_ToggleCCTV     = nullptr;
    UPROPERTY(EditAnywhere, Category = "Input") UInputAction*         IA_FloorUp        = nullptr;
    UPROPERTY(EditAnywhere, Category = "Input") UInputAction*         IA_FloorDown      = nullptr;

    // ── Input callbacks ───────────────────────────────────────────
    void OnPan(const FInputActionValue& Value);
    void OnZoom(const FInputActionValue& Value);
    void OnToggleBuildMode();
    void OnToggleCCTV();
    void OnFloorUp();
    void OnFloorDown();

    // ── State ─────────────────────────────────────────────────────
    EPlayerMode CurrentMode      = EPlayerMode::Management;
    int32       ActiveFloorIndex = 0;
    int32       ActiveCameraIdx  = 0;

    UPROPERTY() class ACameraActor* TopDownCamera = nullptr;

    float MinOrthoWidth     =  800.0f;
    float MaxOrthoWidth     = 6000.0f;
    float CurrentOrthoWidth = 3000.0f;
};
