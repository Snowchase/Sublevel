#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SubLevelTypes.h"
#include "LightFixture.generated.h"

class AParkingFloor;

// ─────────────────────────────────────────────────────────────────
// §8 — ALightFixture
// Contributes light coverage to its floor's visibility grid.
// Without light coverage, a tile cannot be visually observed (dark
// overlay rendered). Camera coverage alone gives incident logging
// but no player-visible real-time awareness.
// ─────────────────────────────────────────────────────────────────

UCLASS()
class SUBLEVEL_API ALightFixture : public AActor
{
    GENERATED_BODY()

public:
    ALightFixture();
    virtual void BeginPlay() override;

    // ── Placement ─────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, Category = "Light|Placement")
    int32 FloorIndex = 0;

    UPROPERTY(EditAnywhere, Category = "Light|Placement")
    int32 CenterTileIndex = 0;         // Tile directly below the fixture

    UPROPERTY(EditAnywhere, Category = "Light|Coverage", meta = (ClampMin = "1", ClampMax = "10"))
    int32 RadiusTiles = 3;

    // ── Runtime state ─────────────────────────────────────────────
    UFUNCTION(BlueprintCallable) ELightState GetLightState() const { return LightState; }
    void SetLightState(ELightState NewState);

    // Called by AParkingFloor::TickLights each SimTick.
    // Handles the probabilistic flicker toggle and notifies floor on change.
    void SimTick(uint64 CurrentTick, FRandomStream& RNG);

    // Returns the set of tiles currently lit (empty when Off, probabilistic when Flickering).
    const TSet<int32>& GetCoveredTiles() const { return CachedCoveredTiles; }

    void RebuildCoverage();

private:
    ELightState LightState         = ELightState::Normal;
    bool        bFlickerIsOn       = true;   // Flicker sub-state this tick
    TSet<int32> CachedCoveredTiles;
    TSet<int32> FullCoveredTiles;            // Pre-computed full set regardless of flicker

    AParkingFloor* GetFloor() const;
    void ComputeFullCoverage();              // Populates FullCoveredTiles
    void PushCoverageToFloor();              // Calls AParkingFloor::RebuildVisibility
};
