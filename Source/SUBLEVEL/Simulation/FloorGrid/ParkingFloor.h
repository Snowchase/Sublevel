#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SubLevelTypes.h"
#include "ParkingFloor.generated.h"

class ASecurityCamera;
class ALightFixture;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

UCLASS()
class SUBLEVEL_API AParkingFloor : public AActor
{
    GENERATED_BODY()

public:
    AParkingFloor();
    virtual void BeginPlay() override;

    // ── Grid Init ────────────────────────────────────────────────
    void InitializeGrid(int32 Width, int32 Height);

    // Builds a simple functional lot: perimeter walls, entry/exit gates,
    // lane corridors, and stall rows. Used for test maps with no hand-built
    // layout. Runs automatically in BeginPlay when the grid is empty and
    // bAutoGenerateTestLayout is set.
    void GenerateDefaultLayout();

    // Instanced-mesh tile visuals from engine basic shapes (test rendering).
    void RebuildTileVisuals();

    // ── Gates ────────────────────────────────────────────────────
    int32 GetEntryGateTile() const { return EntryGateTile; }
    int32 GetExitGateTile()  const { return ExitGateTile; }

    // ── Test layout settings ─────────────────────────────────────
    UPROPERTY(EditAnywhere, Category = "Floor|TestLayout")
    bool bAutoGenerateTestLayout = true;

    UPROPERTY(EditAnywhere, Category = "Floor|TestLayout", meta = (ClampMin = "8", ClampMax = "64"))
    int32 DefaultGridWidth = 24;

    UPROPERTY(EditAnywhere, Category = "Floor|TestLayout", meta = (ClampMin = "8", ClampMax = "64"))
    int32 DefaultGridHeight = 16;

    // ── Tile Access ──────────────────────────────────────────────
    FFloorTile&       GetTile(int32 TileIdx);
    const FFloorTile& GetTile(int32 TileIdx) const;
    FFloorTile&       GetTileXY(int32 Row, int32 Col);

    FORCEINLINE int32 TileIndex(int32 Row, int32 Col) const { return Row * GridWidth + Col; }
    FORCEINLINE bool  IsValidTileIndex(int32 TileIdx) const { return Tiles.IsValidIndex(TileIdx); }
    FORCEINLINE int32 GetGridWidth()  const { return GridWidth; }
    FORCEINLINE int32 GetGridHeight() const { return GridHeight; }
    FORCEINLINE int32 GetTileCount()  const { return Tiles.Num(); }

    // ── Flow Field ───────────────────────────────────────────────
    // Marks the field for this destination dirty; async rebuild fires next SimTick.
    void MarkFlowFieldDirty(uint32 DestTileID);
    void MarkAllFlowFieldsDirty();

    // Called by SimulationSubsystem::TickVehicles before vehicle movement phase.
    // Kicks async Dijkstra tasks for all dirty fields and waits for completion.
    void RebuildDirtyFlowFields();

    const FFlowField* GetFlowFieldForDest(uint32 DestTileID) const;

    // ── Occupancy Reverse Index ───────────────────────────────────
    // O(1) lookups instead of O(n) scan over all vehicles.
    void  AddVehicleToTile(int32 TileIdx, uint32 VehicleID);
    void  RemoveVehicleFromTile(int32 TileIdx, uint32 VehicleID);
    int32 GetVehicleCountOnTile(int32 TileIdx) const;
    const TSet<uint32>* GetVehiclesOnTile(int32 TileIdx) const;

    // Wipes the occupancy index and per-tile occupant/blockage flags.
    // Used by save/load when all agents are destroyed at once.
    void  ClearAllOccupants();

    // ── Visibility ───────────────────────────────────────────────
    FVisibilityGrid&       GetVisibilityGrid()       { return VisibilityGrid; }
    const FVisibilityGrid& GetVisibilityGrid() const { return VisibilityGrid; }

    // Called by ASecurityCamera / ALightFixture in BeginPlay
    void RegisterCamera(ASecurityCamera* Camera);
    void RegisterLight(ALightFixture* Light);

    // Rebuilds CameraCoverage and LightCoverage bitmasks from all registered sources.
    // Called whenever any camera/light changes state or coverage geometry changes.
    void RebuildVisibility();

    // Called by SimulationSubsystem::TickVisibility each SimTick — processes flicker.
    void TickLights(uint64 CurrentTick, FRandomStream& RNG);

    // ── Structural Integrity ─────────────────────────────────────
    void  TickIntegrity();
    float GetStructuralIntegrity() const  { return StructuralIntegrity; }
    void  SetWaterTableRisk(bool bRisk)   { bWaterTableRisk = bRisk; }
    void  AddCrack()    { ++ActiveCrackCount; }
    void  RemoveCrack() { ActiveCrackCount = FMath::Max(0, ActiveCrackCount - 1); }

    // ── World Space Helpers ───────────────────────────────────────
    FVector TileIndexToWorldLocation(int32 TileIdx) const;
    int32   WorldLocationToTileIndex(const FVector& WorldLoc) const;

    // ── Identification ────────────────────────────────────────────
    UPROPERTY(EditAnywhere, Category = "Floor")
    int32 FloorIndex = 0;

    // 100 UU = 1m. Each tile is 1m × 1m. One stall = one tile.
    static constexpr float TileSize = 100.0f;

private:
    int32              GridWidth  = 0;
    int32              GridHeight = 0;
    TArray<FFloorTile> Tiles;

    int32 EntryGateTile = -1;
    int32 ExitGateTile  = -1;

    // ── Test visuals ─────────────────────────────────────────────
    UPROPERTY() USceneComponent* SceneRoot = nullptr;
    UPROPERTY() UStaticMesh* TilePlaneMesh = nullptr;
    UPROPERTY() UStaticMesh* WallCubeMesh  = nullptr;
    UPROPERTY() UMaterialInterface* TileBaseMaterial = nullptr;
    UPROPERTY() TMap<uint8, UInstancedStaticMeshComponent*> TileVisualISMs;   // key = ETileType

    UInstancedStaticMeshComponent* GetOrCreateISMForType(ETileType Type);

    float StructuralIntegrity = 1.0f;
    int32 ActiveCrackCount    = 0;
    bool  bWaterTableRisk     = false;

    FVisibilityGrid VisibilityGrid;

    // §8 — Visibility sources (UPROPERTY keeps GC from collecting them)
    UPROPERTY() TArray<ASecurityCamera*> RegisteredCameras;
    UPROPERTY() TArray<ALightFixture*>   RegisteredLights;

    TMap<uint32, FFlowField>    FlowFieldsByDest;    // DestTileID -> field
    TMap<int32,  TSet<uint32>>  TileOccupants;       // TileIdx -> set of VehicleIDs

    // Async rebuild: dirty fields are recomputed on a Task Graph worker.
    // The game thread waits on the task ref before sampling begins.
    FGraphEventRef PendingFlowFieldTask;
};
