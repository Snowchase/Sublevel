#include "Simulation/FloorGrid/ParkingFloor.h"
#include "Simulation/FloorGrid/SecurityCamera.h"
#include "Simulation/FloorGrid/LightFixture.h"
#include "Simulation/FlowField/FlowField.h"
#include "Subsystems/SimulationSubsystem.h"
#include "Subsystems/EventBusSubsystem.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/World.h"

// ─────────────────────────────────────────────────────────────────
// LIFECYCLE
// ─────────────────────────────────────────────────────────────────

AParkingFloor::AParkingFloor()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AParkingFloor::BeginPlay()
{
    Super::BeginPlay();

    if (USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>())
    {
        Sim->RegisterFloor(FloorIndex, this);
    }
}

// ─────────────────────────────────────────────────────────────────
// GRID INIT
// ─────────────────────────────────────────────────────────────────

void AParkingFloor::InitializeGrid(int32 Width, int32 Height)
{
    GridWidth  = Width;
    GridHeight = Height;

    Tiles.SetNum(Width * Height);
    VisibilityGrid.Initialize(Width * Height);
}

// ─────────────────────────────────────────────────────────────────
// TILE ACCESS
// ─────────────────────────────────────────────────────────────────

FFloorTile& AParkingFloor::GetTile(int32 TileIdx)
{
    check(Tiles.IsValidIndex(TileIdx));
    return Tiles[TileIdx];
}

const FFloorTile& AParkingFloor::GetTile(int32 TileIdx) const
{
    check(Tiles.IsValidIndex(TileIdx));
    return Tiles[TileIdx];
}

FFloorTile& AParkingFloor::GetTileXY(int32 Row, int32 Col)
{
    return GetTile(TileIndex(Row, Col));
}

// ─────────────────────────────────────────────────────────────────
// FLOW FIELD MANAGEMENT
// ─────────────────────────────────────────────────────────────────

void AParkingFloor::MarkFlowFieldDirty(uint32 DestTileID)
{
    if (FFlowField* Field = FlowFieldsByDest.Find(DestTileID))
    {
        Field->bDirty = true;
        Field->bReady = false;
    }
    else
    {
        FFlowField NewField;
        NewField.DestinationTileID = DestTileID;
        NewField.bDirty = true;
        NewField.bReady = false;
        FlowFieldsByDest.Add(DestTileID, MoveTemp(NewField));
    }
}

void AParkingFloor::MarkAllFlowFieldsDirty()
{
    for (auto& [DestID, Field] : FlowFieldsByDest)
    {
        Field.bDirty = true;
        Field.bReady = false;
    }
}

void AParkingFloor::RebuildDirtyFlowFields()
{
    bool bAnyDirty = false;
    for (const auto& [DestID, Field] : FlowFieldsByDest)
    {
        if (Field.bDirty) { bAnyDirty = true; break; }
    }

    if (!bAnyDirty)
        return;

    // Capture a snapshot of the tile array and grid dims for the worker thread.
    // The worker writes into a local copy of the fields map, then we swap back.
    TMap<uint32, FFlowField>  FieldsCopy  = FlowFieldsByDest;
    TArray<FFloorTile>        TilesCopy   = Tiles;
    const int32               CapturedW   = GridWidth;
    const int32               CapturedH   = GridHeight;

    PendingFlowFieldTask = FFunctionGraphTask::CreateAndDispatchWhenReady(
        [this, FieldsCopy = MoveTemp(FieldsCopy), TilesCopy = MoveTemp(TilesCopy), CapturedW, CapturedH]() mutable
        {
            FFlowFieldWorker::Compute(FieldsCopy, TilesCopy, CapturedW, CapturedH);

            // Marshal result back to game thread
            AsyncTask(ENamedThreads::GameThread, [this, Result = MoveTemp(FieldsCopy)]() mutable
            {
                for (auto& [DestID, Field] : Result)
                {
                    if (Field.bReady)
                        FlowFieldsByDest.Add(DestID, MoveTemp(Field));
                }
            });
        },
        TStatId(),
        nullptr,
        ENamedThreads::AnyBackgroundThreadNormalTask
    );
}

const FFlowField* AParkingFloor::GetFlowFieldForDest(uint32 DestTileID) const
{
    const FFlowField* Field = FlowFieldsByDest.Find(DestTileID);
    return (Field && Field->bReady) ? Field : nullptr;
}

// ─────────────────────────────────────────────────────────────────
// OCCUPANCY REVERSE INDEX
// ─────────────────────────────────────────────────────────────────

void AParkingFloor::AddVehicleToTile(int32 TileIdx, uint32 VehicleID)
{
    TileOccupants.FindOrAdd(TileIdx).Add(VehicleID);
    if (Tiles.IsValidIndex(TileIdx))
    {
        Tiles[TileIdx].bOccupied  = true;
        Tiles[TileIdx].OccupantID = VehicleID; // Last writer for single-occupancy stalls
    }
}

void AParkingFloor::RemoveVehicleFromTile(int32 TileIdx, uint32 VehicleID)
{
    if (TSet<uint32>* Set = TileOccupants.Find(TileIdx))
    {
        Set->Remove(VehicleID);
        if (Set->IsEmpty())
        {
            TileOccupants.Remove(TileIdx);
            if (Tiles.IsValidIndex(TileIdx))
            {
                Tiles[TileIdx].bOccupied  = false;
                Tiles[TileIdx].OccupantID = 0;
            }
        }
    }
}

int32 AParkingFloor::GetVehicleCountOnTile(int32 TileIdx) const
{
    const TSet<uint32>* Set = TileOccupants.Find(TileIdx);
    return Set ? Set->Num() : 0;
}

// ─────────────────────────────────────────────────────────────────
// VISIBILITY — Registration and Rebuild
// ─────────────────────────────────────────────────────────────────

void AParkingFloor::RegisterCamera(ASecurityCamera* Camera)
{
    if (Camera && !RegisteredCameras.Contains(Camera))
        RegisteredCameras.Add(Camera);
}

void AParkingFloor::RegisterLight(ALightFixture* Light)
{
    if (Light && !RegisteredLights.Contains(Light))
        RegisteredLights.Add(Light);
}

void AParkingFloor::RebuildVisibility()
{
    const int32 TileCount = Tiles.Num();
    if (TileCount == 0) return;

    VisibilityGrid.CameraCoverage.Init(false, TileCount);
    VisibilityGrid.LightCoverage.Init(false, TileCount);

    for (const ASecurityCamera* Cam : RegisteredCameras)
    {
        if (!Cam || Cam->GetCameraState() == ECameraState::Offline) continue;
        for (int32 TileIdx : Cam->GetCoveredTiles())
        {
            if (VisibilityGrid.CameraCoverage.IsValidIndex(TileIdx))
                VisibilityGrid.CameraCoverage[TileIdx] = true;
        }
    }

    for (const ALightFixture* Light : RegisteredLights)
    {
        if (!Light || Light->GetLightState() == ELightState::Off) continue;
        for (int32 TileIdx : Light->GetCoveredTiles())
        {
            if (VisibilityGrid.LightCoverage.IsValidIndex(TileIdx))
                VisibilityGrid.LightCoverage[TileIdx] = true;
        }
    }
}

void AParkingFloor::TickLights(uint64 CurrentTick, FRandomStream& RNG)
{
    for (ALightFixture* Light : RegisteredLights)
    {
        if (Light) Light->SimTick(CurrentTick, RNG);
    }
}

const TSet<uint32>* AParkingFloor::GetVehiclesOnTile(int32 TileIdx) const
{
    return TileOccupants.Find(TileIdx);
}

// ─────────────────────────────────────────────────────────────────
// STRUCTURAL INTEGRITY
// ─────────────────────────────────────────────────────────────────

void AParkingFloor::TickIntegrity()
{
    // Passive decay — very slow baseline
    StructuralIntegrity -= 0.000005f;

    // Deeper floors degrade faster
    StructuralIntegrity -= FloorIndex * 0.000002f;

    // Active crack events multiply decay until resolved
    if (ActiveCrackCount > 0)
        StructuralIntegrity -= ActiveCrackCount * 0.00005f;

    StructuralIntegrity = FMath::Clamp(StructuralIntegrity, 0.0f, 1.0f);

    // Broadcast integrity change for UI and city system
    if (UEventBusSubsystem* Bus = UEventBusSubsystem::Get(this))
    {
        Bus->OnIntegrityChanged.Broadcast(FloorIndex, StructuralIntegrity);

        if (StructuralIntegrity <= 0.0f)
            Bus->OnFloorCollapse.Broadcast(FloorIndex);
    }

    // Congestion cost update — runs every tick so the flow field can use live costs
    for (int32 Idx = 0; Idx < Tiles.Num(); ++Idx)
    {
        FFloorTile& Tile = Tiles[Idx];
        if (Tile.Type == ETileType::Lane)
        {
            const int32 VehicleCount = GetVehicleCountOnTile(Idx);
            const float Density      = FMath::Min((float)VehicleCount / 4.0f, 1.0f); // cap at 4 vehicles
            const float NewCost      = FMath::Lerp(1.0f, 8.0f, Density);

            if (!FMath::IsNearlyEqual(Tile.CostModifier, NewCost, 0.1f))
            {
                Tile.CostModifier = NewCost;
                MarkAllFlowFieldsDirty(); // Re-route around congestion next rebuild
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// WORLD SPACE
// ─────────────────────────────────────────────────────────────────

FVector AParkingFloor::TileIndexToWorldLocation(int32 TileIdx) const
{
    const int32 Row = TileIdx / GridWidth;
    const int32 Col = TileIdx % GridWidth;

    // Floor B1 (index 0) at Z=0; each floor is 400 UU below the previous.
    const FVector Origin = GetActorLocation();
    return FVector(
        Origin.X + Col * TileSize + TileSize * 0.5f,
        Origin.Y + Row * TileSize + TileSize * 0.5f,
        Origin.Z
    );
}

int32 AParkingFloor::WorldLocationToTileIndex(const FVector& WorldLoc) const
{
    const FVector Origin = GetActorLocation();
    const int32 Col = FMath::FloorToInt((WorldLoc.X - Origin.X) / TileSize);
    const int32 Row = FMath::FloorToInt((WorldLoc.Y - Origin.Y) / TileSize);

    if (Col < 0 || Col >= GridWidth || Row < 0 || Row >= GridHeight)
        return -1;

    return Row * GridWidth + Col;
}
