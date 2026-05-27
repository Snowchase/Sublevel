#include "Simulation/FlowField/FlowField.h"

// Direction offsets for N / E / S / W
static const int32 GDirX[4] = {  0, 1,  0, -1 };
static const int32 GDirY[4] = { -1, 0,  1,  0 };

// ─────────────────────────────────────────────────────────────────
// PUBLIC — Compute all dirty fields in the map
// ─────────────────────────────────────────────────────────────────

void FFlowFieldWorker::Compute(
    TMap<uint32, FFlowField>& Fields,
    const TArray<FFloorTile>&  Tiles,
    int32                      GridWidth,
    int32                      GridHeight)
{
    for (auto& [DestID, Field] : Fields)
    {
        if (Field.bDirty)
        {
            ComputeSingle(Field, Tiles, GridWidth, GridHeight);
            Field.bDirty = false;
            Field.bReady = true;
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// PRIVATE — Dijkstra BFS from destination outward
// ─────────────────────────────────────────────────────────────────

void FFlowFieldWorker::ComputeSingle(
    FFlowField&               OutField,
    const TArray<FFloorTile>& Tiles,
    int32                     GridWidth,
    int32                     GridHeight)
{
    const int32 TileCount = Tiles.Num();

    OutField.CostField.Init(TNumericLimits<float>::Max(), TileCount);
    OutField.Directions.Init(FVector2D::ZeroVector, TileCount);

    // Validate destination tile
    if (!Tiles.IsValidIndex((int32)OutField.DestinationTileID))
        return;

    // BFS priority queue — (cost, tileIdx)
    TArray<TPair<float, int32>> OpenSet;
    OpenSet.Heapify();

    OutField.CostField[OutField.DestinationTileID] = 0.f;
    OpenSet.HeapPush({ 0.f, (int32)OutField.DestinationTileID });

    while (OpenSet.Num() > 0)
    {
        TPair<float, int32> Current;
        OpenSet.HeapPop(Current);

        const float CurrentCost = Current.Key;
        const int32 CurrentIdx  = Current.Value;

        if (CurrentCost > OutField.CostField[CurrentIdx])
            continue; // Stale entry

        const int32 Row = CurrentIdx / GridWidth;
        const int32 Col = CurrentIdx % GridWidth;

        for (int32 D = 0; D < 4; ++D)
        {
            const int32 NRow = Row + GDirY[D];
            const int32 NCol = Col + GDirX[D];

            if (NRow < 0 || NRow >= GridHeight || NCol < 0 || NCol >= GridWidth)
                continue;

            const int32 NeighbourIdx = NRow * GridWidth + NCol;
            const FFloorTile& Neighbour = Tiles[NeighbourIdx];

            if (!Neighbour.IsPassable())
                continue;

            // Check that the neighbour tile permits entry from this direction.
            // A vehicle moving North (DirY=-1) enters from the South face of the neighbour.
            const uint8 EntryFlag = LaneFlagForDir(-GDirX[D], -GDirY[D]);
            if (Neighbour.LaneFlags != ELaneFlag::All &&
                (Neighbour.LaneFlags & EntryFlag) == 0)
                continue;

            const float NewCost = CurrentCost + Neighbour.CostModifier;
            if (NewCost < OutField.CostField[NeighbourIdx])
            {
                OutField.CostField[NeighbourIdx] = NewCost;
                OpenSet.HeapPush({ NewCost, NeighbourIdx });
            }
        }
    }

    // Pass 2: derive direction from cost field.
    // Each tile points toward the neighbour with the lowest cost.
    for (int32 TileIdx = 0; TileIdx < TileCount; ++TileIdx)
    {
        if (OutField.CostField[TileIdx] >= TNumericLimits<float>::Max())
            continue; // Unreachable

        if (TileIdx == (int32)OutField.DestinationTileID)
        {
            OutField.Directions[TileIdx] = FVector2D::ZeroVector;
            continue;
        }

        float    BestCost = TNumericLimits<float>::Max();
        FVector2D BestDir = FVector2D::ZeroVector;

        const int32 Row = TileIdx / GridWidth;
        const int32 Col = TileIdx % GridWidth;

        for (int32 D = 0; D < 4; ++D)
        {
            const int32 NRow = Row + GDirY[D];
            const int32 NCol = Col + GDirX[D];

            if (NRow < 0 || NRow >= GridHeight || NCol < 0 || NCol >= GridWidth)
                continue;

            const int32 NeighbourIdx = NRow * GridWidth + NCol;
            if (OutField.CostField[NeighbourIdx] < BestCost)
            {
                BestCost = OutField.CostField[NeighbourIdx];
                BestDir  = FVector2D((float)GDirX[D], (float)GDirY[D]);
            }
        }

        OutField.Directions[TileIdx] = BestDir; // Already unit length (cardinal)
    }
}

uint8 FFlowFieldWorker::LaneFlagForDir(int32 DirX, int32 DirY)
{
    if (DirY < 0) return ELaneFlag::North;
    if (DirX > 0) return ELaneFlag::East;
    if (DirY > 0) return ELaneFlag::South;
    if (DirX < 0) return ELaneFlag::West;
    return 0;
}

int32 FFlowFieldWorker::NeighbourInDirection(
    int32                     TileIdx,
    int32                     DirX,
    int32                     DirY,
    const TArray<FFloorTile>& Tiles,
    int32                     GridWidth,
    int32                     GridHeight)
{
    const int32 Row  = TileIdx / GridWidth;
    const int32 Col  = TileIdx % GridWidth;
    const int32 NRow = Row + DirY;
    const int32 NCol = Col + DirX;

    if (NRow < 0 || NRow >= GridHeight || NCol < 0 || NCol >= GridWidth)
        return -1;

    const int32 NeighbourIdx = NRow * GridWidth + NCol;
    return Tiles[NeighbourIdx].IsPassable() ? NeighbourIdx : -1;
}
