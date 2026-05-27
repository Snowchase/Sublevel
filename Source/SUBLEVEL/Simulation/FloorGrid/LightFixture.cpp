#include "Simulation/FloorGrid/LightFixture.h"
#include "Simulation/FloorGrid/ParkingFloor.h"
#include "Subsystems/SimulationSubsystem.h"
#include "Engine/World.h"

ALightFixture::ALightFixture()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ALightFixture::BeginPlay()
{
    Super::BeginPlay();

    if (AParkingFloor* Floor = GetFloor())
    {
        Floor->RegisterLight(this);
        ComputeFullCoverage();
        RebuildCoverage();
        PushCoverageToFloor();
    }
}

// ─────────────────────────────────────────────────────────────────
// STATE MANAGEMENT
// ─────────────────────────────────────────────────────────────────

void ALightFixture::SetLightState(ELightState NewState)
{
    if (LightState == NewState) return;
    LightState = NewState;

    if (NewState == ELightState::Off)
        CachedCoveredTiles.Empty();
    else if (NewState == ELightState::Normal)
        CachedCoveredTiles = FullCoveredTiles;
    // Flickering is handled per-tick in SimTick

    PushCoverageToFloor();
}

// ─────────────────────────────────────────────────────────────────
// SIM TICK — flicker processing
// ─────────────────────────────────────────────────────────────────

void ALightFixture::SimTick(uint64 CurrentTick, FRandomStream& RNG)
{
    if (LightState != ELightState::Flickering) return;

    const bool bWasOn  = bFlickerIsOn;
    bFlickerIsOn       = RNG.FRand() >= 0.5f;  // 50% chance each tick

    if (bFlickerIsOn != bWasOn)
    {
        CachedCoveredTiles = bFlickerIsOn ? FullCoveredTiles : TSet<int32>();
        PushCoverageToFloor();
    }
}

// ─────────────────────────────────────────────────────────────────
// COVERAGE COMPUTATION
// ─────────────────────────────────────────────────────────────────

void ALightFixture::ComputeFullCoverage()
{
    FullCoveredTiles.Empty();

    const AParkingFloor* Floor = GetFloor();
    if (!Floor || !Floor->IsValidTileIndex(CenterTileIndex)) return;

    const int32 GridWidth  = Floor->GetGridWidth();
    const int32 GridHeight = Floor->GetGridHeight();
    const int32 CenterRow  = CenterTileIndex / GridWidth;
    const int32 CenterCol  = CenterTileIndex % GridWidth;
    const float RadiusSq   = (float)(RadiusTiles * RadiusTiles);

    for (int32 Row = 0; Row < GridHeight; ++Row)
    {
        for (int32 Col = 0; Col < GridWidth; ++Col)
        {
            const float DR = (float)(Row - CenterRow);
            const float DC = (float)(Col - CenterCol);
            if (DR * DR + DC * DC <= RadiusSq)
                FullCoveredTiles.Add(Row * GridWidth + Col);
        }
    }
}

void ALightFixture::RebuildCoverage()
{
    switch (LightState)
    {
        case ELightState::Normal:
            CachedCoveredTiles = FullCoveredTiles;
            break;
        case ELightState::Flickering:
            CachedCoveredTiles = bFlickerIsOn ? FullCoveredTiles : TSet<int32>();
            break;
        case ELightState::Off:
            CachedCoveredTiles.Empty();
            break;
    }
}

void ALightFixture::PushCoverageToFloor()
{
    if (AParkingFloor* Floor = GetFloor())
        Floor->RebuildVisibility();
}

AParkingFloor* ALightFixture::GetFloor() const
{
    if (USimulationSubsystem* Sim = GetWorld()->GetSubsystem<USimulationSubsystem>())
        return Sim->GetFloor(FloorIndex);
    return nullptr;
}
