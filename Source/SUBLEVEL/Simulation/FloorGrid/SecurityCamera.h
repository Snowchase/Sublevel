#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SubLevelTypes.h"
#include "SecurityCamera.generated.h"

class AParkingFloor;

// ─────────────────────────────────────────────────────────────────
// §8 — ASecurityCamera
// Defines a directional coverage cone on its floor's visibility grid.
// Camera coverage + light coverage must BOTH be present for a tile to be
// fully observable (incident detection active).
// ─────────────────────────────────────────────────────────────────

UCLASS()
class SUBLEVEL_API ASecurityCamera : public AActor
{
    GENERATED_BODY()

public:
    ASecurityCamera();
    virtual void BeginPlay() override;

    // ── Placement (set in editor or procedurally) ─────────────────
    UPROPERTY(EditAnywhere, Category = "Camera|Placement")
    int32 FloorIndex = 0;

    // Tile index the camera is mounted at (looks outward from here)
    UPROPERTY(EditAnywhere, Category = "Camera|Placement")
    int32 MountTileIndex = 0;

    // Normalized 2D facing direction in tile space (e.g. (1,0) = East)
    UPROPERTY(EditAnywhere, Category = "Camera|Placement")
    FVector2D FacingDirection = FVector2D(1.f, 0.f);

    // ── Coverage parameters ───────────────────────────────────────
    UPROPERTY(EditAnywhere, Category = "Camera|Coverage", meta = (ClampMin = "10", ClampMax = "180"))
    float ConeHalfAngleDeg = 45.f;     // Half-angle of the coverage cone

    UPROPERTY(EditAnywhere, Category = "Camera|Coverage", meta = (ClampMin = "1", ClampMax = "20"))
    int32 RangeTiles = 8;              // Max detection range in tiles

    // ── Identity ──────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, Category = "Camera")
    int32 CameraID = 0;               // Used for CCTV tab cycling and alert pips

    // ── Runtime state ─────────────────────────────────────────────
    UFUNCTION(BlueprintCallable) ECameraState GetCameraState() const { return CameraState; }
    void SetCameraState(ECameraState NewState);

    // Returns the cached set of tile indices this camera covers.
    // Rebuilt whenever state changes or floor grid is modified.
    const TSet<int32>& GetCoveredTiles() const { return CachedCoveredTiles; }

    // Recomputes CachedCoveredTiles from current floor grid state.
    void RebuildCoverage();

private:
    ECameraState CameraState = ECameraState::Active;
    TSet<int32>  CachedCoveredTiles;

    AParkingFloor* GetFloor() const;

    // Returns true if the tile at (TileRow, TileCol) falls within
    // this camera's active cone from (MountRow, MountCol).
    bool TileInCone(int32 MountRow, int32 MountCol,
                    int32 TileRow,  int32 TileCol) const;
};
