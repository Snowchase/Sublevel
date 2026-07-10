#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SubLevelTypes.h"
#include "VehicleAgent.generated.h"

class AParkingFloor;

UCLASS()
class SUBLEVEL_API AVehicleAgent : public AActor
{
    GENERATED_BODY()

public:
    AVehicleAgent();

    void Initialize(uint32 InVehicleID, EVehicleType InType, int32 InFloorIndex, int32 StartTileID);

    // §14 — save/load: place this vehicle directly into the Parked state.
    void RestoreParkedState(uint64 ParkStartTick, uint64 DespawnTick);

    // Engine tick: visual interpolation only — never advances sim state.
    virtual void Tick(float DeltaTime) override;

    // Called once per SimTick by USimulationSubsystem::TickVehicles.
    void SimTick(uint64 CurrentTick);

    // ── Accessors ─────────────────────────────────────────────────
    const FVehicleData& GetVehicleData()  const { return VehicleData; }
    uint32              GetVehicleID()    const { return VehicleData.ID; }
    EVehicleState       GetState()        const { return VehicleData.State; }
    int32               GetCurrentFloor() const { return VehicleData.FloorIndex; }

private:
    FVehicleData VehicleData;

    // ── Visual interpolation ──────────────────────────────────────
    // The engine tick smoothly moves the mesh between sim-tick positions.
    FVector WorldPosLast;    // position at previous SimTick
    FVector WorldPosNext;    // position at current SimTick (target)
    float   InterpAlpha = 0.f;

    static constexpr float SimTickInterval = 0.05f;   // must match SimulationSubsystem

    // ── FSM ───────────────────────────────────────────────────────
    void FSM_Spawning(uint64 CurrentTick);
    void FSM_Seeking(uint64 CurrentTick);
    void FSM_Queuing(uint64 CurrentTick);
    void FSM_Parking(uint64 CurrentTick);
    void FSM_Parked(uint64 CurrentTick);
    void FSM_Circling(uint64 CurrentTick);
    void FSM_Exiting(uint64 CurrentTick);

    void TransitionTo(EVehicleState NewState, uint64 CurrentTick);

    // ── Movement helpers ─────────────────────────────────────────
    // Returns the best adjacent tile index by sampling DestTileID's flow field.
    // Returns -1 if no valid direction or flow field not ready.
    int32 SampleFlowFieldNextTile(AParkingFloor* Floor, uint32 DestTileID) const;

    // Moves vehicle from CurrentTileID to NextTileID — updates occupancy index.
    void MoveToTile(AParkingFloor* Floor, int32 NextTileID);

    // Finds the nearest unoccupied stall that matches VehicleData.Type.
    // Returns -1 if none available on this floor.
    int32 FindNearestAvailableStall(AParkingFloor* Floor) const;

    // Converts a tile index to its world-space center location.
    FVector TileToWorldLoc(AParkingFloor* Floor, int32 TileIdx) const;
    AParkingFloor* GetFloor() const;

    // Clears this vehicle from the floor's occupancy index.
    void RemoveVehicleFromFloor(AParkingFloor* Floor);

    // ── Stall reservation ────────────────────────────────────────
    int32 ReservedStallTileID = -1;

    // Ticks spent in Seeking without finding a stall → triggers Circling.
    int32 TicksWithoutStall = 0;
    static constexpr int32 SeekTimeoutTicks = 200;

    // Movement rate: ticks per tile traversal at 1.0× speed modifier.
    static constexpr float TicksPerTile = 4.0f;
    float MoveCooldown = 0.f;

    UPROPERTY()
    UStaticMeshComponent* MeshComponent = nullptr;
};
