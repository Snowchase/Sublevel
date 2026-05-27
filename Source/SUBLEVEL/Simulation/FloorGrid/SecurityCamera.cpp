#include "Simulation/FloorGrid/SecurityCamera.h"
#include "Simulation/FloorGrid/ParkingFloor.h"
#include "Subsystems/SimulationSubsystem.h"
#include "Engine/World.h"

ASecurityCamera::ASecurityCamera()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ASecurityCamera::BeginPlay()
{
    Super::BeginPlay();

    if (AParkingFloor* Floor = GetFloor())
    {
        Floor->RegisterCamera(this);
        RebuildCoverage();
    }
}

// ─────────────────────────────────────────────────────────────────
// STATE MANAGEMENT
// ─────────────────────────────────────────────────────────────────

void ASecurityCamera::SetCameraState(ECameraState NewState)
{
    if (CameraState == NewState) return;
    CameraState = NewState;

    RebuildCoverage();

    if (AParkingFloor* Floor = GetFloor())
        Floor->RebuildVisibility();
}

// ─────────────────────────────────────────────────────────────────
// COVERAGE COMPUTATION
// ─────────────────────────────────────────────────────────────────

void ASecurityCamera::RebuildCoverage()
{
    CachedCoveredTiles.Empty();

    if (CameraState == ECameraState::Offline)
        return;

    const AParkingFloor* Floor = GetFloor();
    if (!Floor) return;

    const int32 GridWidth  = Floor->GetGridWidth();
    const int32 GridHeight = Floor->GetGridHeight();

    if (!Floor->IsValidTileIndex(MountTileIndex)) return;

    const int32 MountRow = MountTileIndex / GridWidth;
    const int32 MountCol = MountTileIndex % GridWidth;

    // Degraded cameras have half the normal cone angle
    const float EffectiveHalfAngle = (CameraState == ECameraState::Degraded)
        ? ConeHalfAngleDeg * 0.5f
        : ConeHalfAngleDeg;

    for (int32 Row = 0; Row < GridHeight; ++Row)
    {
        for (int32 Col = 0; Col < GridWidth; ++Col)
        {
            const int32 TileIdx = Row * GridWidth + Col;

            if (!Floor->GetTile(TileIdx).IsPassable() &&
                Floor->GetTile(TileIdx).Type != ETileType::Wall)
                continue; // Walls block and are not "covered"

            if (TileInCone(MountRow, MountCol, Row, Col))
                CachedCoveredTiles.Add(TileIdx);
        }
    }

    // Always include the mount tile itself
    CachedCoveredTiles.Add(MountTileIndex);
}

bool ASecurityCamera::TileInCone(int32 MountRow, int32 MountCol,
                                  int32 TileRow,  int32 TileCol) const
{
    const float DRow = (float)(TileRow - MountRow);
    const float DCol = (float)(TileCol - MountCol);

    const float DistSq = DRow * DRow + DCol * DCol;
    if (DistSq > (float)(RangeTiles * RangeTiles))
        return false;

    // Mount tile is always in range (distance = 0)
    if (DistSq < KINDA_SMALL_NUMBER)
        return true;

    // Dot product gives cosine of angle between tile direction and facing
    const float InvDist = FMath::InvSqrt(DistSq);
    const float Dot     = (DCol * FacingDirection.X + DRow * FacingDirection.Y) * InvDist;

    const float EffectiveHalfAngle = (CameraState == ECameraState::Degraded)
        ? ConeHalfAngleDeg * 0.5f
        : ConeHalfAngleDeg;

    const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(EffectiveHalfAngle));
    return Dot >= CosHalfAngle;
}

// ─────────────────────────────────────────────────────────────────
// HELPERS
// ─────────────────────────────────────────────────────────────────

AParkingFloor* ASecurityCamera::GetFloor() const
{
    if (USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>())
        return Sim->GetFloor(FloorIndex);
    return nullptr;
}
