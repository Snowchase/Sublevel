#include "Simulation/FloorGrid/ParkingFloor.h"
#include "Simulation/FloorGrid/SecurityCamera.h"
#include "Simulation/FloorGrid/LightFixture.h"
#include "Simulation/FlowField/FlowField.h"
#include "Subsystems/SimulationSubsystem.h"
#include "Subsystems/EventBusSubsystem.h"
#include "Async/TaskGraphInterfaces.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"

// ─────────────────────────────────────────────────────────────────
// LIFECYCLE
// ─────────────────────────────────────────────────────────────────

AParkingFloor::AParkingFloor()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    // Engine basic shapes — always available, no content required.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial"));

    if (PlaneFinder.Succeeded())    TilePlaneMesh    = PlaneFinder.Object;
    if (CubeFinder.Succeeded())     WallCubeMesh     = CubeFinder.Object;
    if (MaterialFinder.Succeeded()) TileBaseMaterial = MaterialFinder.Object;
}

void AParkingFloor::BeginPlay()
{
    Super::BeginPlay();

    // Test builds: give an empty floor a working layout so the sim has
    // something to run on without hand-authored level content.
    if (Tiles.Num() == 0 && bAutoGenerateTestLayout)
    {
        InitializeGrid(DefaultGridWidth, DefaultGridHeight);
        GenerateDefaultLayout();
        RebuildTileVisuals();
    }

    RebuildVisibility();

    if (ExitGateTile >= 0)
        MarkFlowFieldDirty((uint32)ExitGateTile);   // exit field ready before first vehicle

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
// DEFAULT TEST LAYOUT
//
//   row 0        : wall  E=entry  X=exit  wall
//   rows 1-2     : lane corridor (connects the gates)
//   rows 3..H-3  : [stall][stall][lane] repeating blocks
//   row H-2      : lane corridor (bottom return)
//   row H-1      : wall
//   cols 0,W-1   : wall;  cols 1-2 and W-3..W-2: vertical lanes
// ─────────────────────────────────────────────────────────────────

void AParkingFloor::GenerateDefaultLayout()
{
    if (GridWidth < 8 || GridHeight < 8) return;

    const int32 W = GridWidth;
    const int32 H = GridHeight;

    auto SetTile = [this](int32 Row, int32 Col, ETileType Type)
    {
        FFloorTile& Tile = GetTileXY(Row, Col);
        Tile.Type      = Type;
        Tile.LaneFlags = ELaneFlag::All;
    };

    // Perimeter walls
    for (int32 Col = 0; Col < W; ++Col)
    {
        SetTile(0,     Col, ETileType::Wall);
        SetTile(H - 1, Col, ETileType::Wall);
    }
    for (int32 Row = 0; Row < H; ++Row)
    {
        SetTile(Row, 0,     ETileType::Wall);
        SetTile(Row, W - 1, ETileType::Wall);
    }

    // Interior: lanes + stall rows
    for (int32 Row = 1; Row < H - 1; ++Row)
    {
        for (int32 Col = 1; Col < W - 1; ++Col)
        {
            const bool bVerticalLane   = (Col <= 2) || (Col >= W - 3);
            const bool bTopCorridor    = (Row <= 2);
            const bool bBottomCorridor = (Row == H - 2);
            const bool bLaneRow        = ((Row - 3) % 3 == 2);

            if (bVerticalLane || bTopCorridor || bBottomCorridor || bLaneRow)
            {
                SetTile(Row, Col, ETileType::Lane);
                continue;
            }

            // Stall type by column band
            ETileType StallType = ETileType::Stall_Standard;
            if      (Col <= 6)       StallType = ETileType::Stall_Compact;
            else if (Col <= 14)      StallType = ETileType::Stall_Standard;
            else if (Col <= 17)      StallType = ETileType::Stall_Oversized;
            else if (Col == 18)      StallType = ETileType::Stall_Motorcycle;
            else if (Col == 19)      StallType = ETileType::Stall_Disabled;

            SetTile(Row, Col, StallType);
        }
    }

    // Gates on the top wall, opening into the top corridor
    const int32 EntryCol = FMath::Clamp(W / 4,     1, W - 2);
    const int32 ExitCol  = FMath::Clamp(3 * W / 4, 1, W - 2);
    SetTile(0, EntryCol, ETileType::EntryGate);
    SetTile(0, ExitCol,  ETileType::ExitGate);

    EntryGateTile = TileIndex(0, EntryCol);
    ExitGateTile  = TileIndex(0, ExitCol);

    UE_LOG(LogTemp, Log, TEXT("[ParkingFloor %d] Generated %dx%d test layout (entry=%d exit=%d)"),
        FloorIndex, W, H, EntryGateTile, ExitGateTile);
}

// ─────────────────────────────────────────────────────────────────
// TEST VISUALS — instanced planes/cubes from engine basic shapes
// ─────────────────────────────────────────────────────────────────

namespace
{
    FLinearColor TileColor(ETileType Type)
    {
        switch (Type)
        {
            case ETileType::Lane:             return FLinearColor(0.10f, 0.10f, 0.12f);
            case ETileType::Stall_Standard:   return FLinearColor(0.16f, 0.22f, 0.30f);
            case ETileType::Stall_Compact:    return FLinearColor(0.14f, 0.28f, 0.26f);
            case ETileType::Stall_Oversized:  return FLinearColor(0.30f, 0.22f, 0.12f);
            case ETileType::Stall_Motorcycle: return FLinearColor(0.24f, 0.16f, 0.30f);
            case ETileType::Stall_Disabled:   return FLinearColor(0.10f, 0.25f, 0.45f);
            case ETileType::EntryGate:        return FLinearColor(0.10f, 0.45f, 0.12f);
            case ETileType::ExitGate:         return FLinearColor(0.45f, 0.10f, 0.10f);
            case ETileType::Ramp_Up:
            case ETileType::Ramp_Down:        return FLinearColor(0.40f, 0.35f, 0.10f);
            case ETileType::Wall:             return FLinearColor(0.35f, 0.35f, 0.38f);
            default:                          return FLinearColor::Black;
        }
    }
}

UInstancedStaticMeshComponent* AParkingFloor::GetOrCreateISMForType(ETileType Type)
{
    if (UInstancedStaticMeshComponent** Found = TileVisualISMs.Find((uint8)Type))
        return *Found;

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
    ISM->SetupAttachment(RootComponent);
    ISM->RegisterComponent();
    ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ISM->SetStaticMesh(Type == ETileType::Wall ? WallCubeMesh : TilePlaneMesh);

    if (TileBaseMaterial)
    {
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(TileBaseMaterial, this);
        MID->SetVectorParameterValue(TEXT("Color"), TileColor(Type));
        ISM->SetMaterial(0, MID);
    }

    TileVisualISMs.Add((uint8)Type, ISM);
    return ISM;
}

void AParkingFloor::RebuildTileVisuals()
{
    if (!TilePlaneMesh || !WallCubeMesh) return;

    for (auto& [TypeKey, ISM] : TileVisualISMs)
    {
        if (ISM) ISM->ClearInstances();
    }

    for (int32 Idx = 0; Idx < Tiles.Num(); ++Idx)
    {
        const ETileType Type = Tiles[Idx].Type;
        if (Type == ETileType::Empty) continue;

        UInstancedStaticMeshComponent* ISM = GetOrCreateISMForType(Type);
        if (!ISM) continue;

        const FVector Center = TileIndexToWorldLocation(Idx);
        FTransform InstanceTransform;

        if (Type == ETileType::Wall)
        {
            // Cube is 100³ at scale 1 — raise it half a tile and stretch upward
            InstanceTransform = FTransform(
                FRotator::ZeroRotator,
                Center + FVector(0, 0, 100.f),
                FVector(1.f, 1.f, 2.f));
        }
        else
        {
            // Plane is 100×100 at scale 1 — exactly one tile
            InstanceTransform = FTransform(
                FRotator::ZeroRotator,
                Center,
                FVector(0.96f, 0.96f, 1.f));   // slight inset shows grid lines
        }

        ISM->AddInstance(InstanceTransform, /*bWorldSpace =*/ true);
    }
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

    // Test-build fallback: with no cameras or lights placed, treat the whole
    // floor as visible so incidents surface and the sim loop is playable.
    if (RegisteredCameras.Num() == 0 && RegisteredLights.Num() == 0)
    {
        VisibilityGrid.CameraCoverage.Init(true, TileCount);
        VisibilityGrid.LightCoverage.Init(true, TileCount);
        return;
    }

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

void AParkingFloor::ClearAllOccupants()
{
    TileOccupants.Empty();
    for (FFloorTile& Tile : Tiles)
    {
        Tile.bOccupied  = false;
        Tile.bBlocked   = false;
        Tile.OccupantID = 0;
    }
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
