#pragma once

#include "CoreMinimal.h"
#include "SubLevelTypes.h"

// Computes flow field directions for a given destination tile via Dijkstra BFS.
// Designed to run on a Task Graph worker thread. Result is written into the
// provided FFlowField; caller reads it only after the task ref completes.
class SUBLEVEL_API FFlowFieldWorker
{
public:
    // Compute all flow fields in FieldsToRebuild in-place.
    // GridWidth, GridHeight, and Tiles are captured by value for thread safety.
    static void Compute(
        TMap<uint32, FFlowField>& Fields,
        const TArray<FFloorTile>&  Tiles,
        int32                      GridWidth,
        int32                      GridHeight
    );

private:
    static void ComputeSingle(
        FFlowField&               OutField,
        const TArray<FFloorTile>& Tiles,
        int32                     GridWidth,
        int32                     GridHeight
    );

    // Returns neighbour tile indices reachable from TileIdx in the given direction,
    // respecting LaneFlags. Returns -1 for out-of-bounds or impassable.
    static int32 NeighbourInDirection(
        int32                     TileIdx,
        int32                     DirX,
        int32                     DirY,
        const TArray<FFloorTile>& Tiles,
        int32                     GridWidth,
        int32                     GridHeight
    );

    // LaneFlag bit for each (DirX, DirY) offset.
    static uint8 LaneFlagForDir(int32 DirX, int32 DirY);
};
