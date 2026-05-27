#pragma once

#include "CoreMinimal.h"
#include "SubLevelTypes.generated.h"

// ─────────────────────────────────────────────────────────────────
// TILE
// ─────────────────────────────────────────────────────────────────

UENUM(BlueprintType)
enum class ETileType : uint8
{
    Empty           UMETA(DisplayName = "Empty"),
    Lane            UMETA(DisplayName = "Lane"),
    Stall_Standard  UMETA(DisplayName = "Stall - Standard"),
    Stall_Compact   UMETA(DisplayName = "Stall - Compact"),
    Stall_Oversized UMETA(DisplayName = "Stall - Oversized"),
    Stall_Motorcycle UMETA(DisplayName = "Stall - Motorcycle"),
    Stall_Disabled  UMETA(DisplayName = "Stall - Disabled (ADA)"),
    Ramp_Up         UMETA(DisplayName = "Ramp Up"),
    Ramp_Down       UMETA(DisplayName = "Ramp Down"),
    EntryGate       UMETA(DisplayName = "Entry Gate"),
    ExitGate        UMETA(DisplayName = "Exit Gate"),
    Wall            UMETA(DisplayName = "Wall")
};

// Bitmask — which cardinal directions vehicles may enter this tile from
namespace ELaneFlag
{
    constexpr uint8 North = 1 << 0;
    constexpr uint8 East  = 1 << 1;
    constexpr uint8 South = 1 << 2;
    constexpr uint8 West  = 1 << 3;
    constexpr uint8 All   = North | East | South | West;
}

USTRUCT()
struct FFloorTile
{
    GENERATED_BODY()

    ETileType   Type          = ETileType::Empty;
    uint8       LaneFlags     = 0;        // ELaneFlag bitmask
    bool        bOccupied     = false;
    bool        bBlocked      = false;    // Incident-caused blockage
    float       CostModifier  = 1.0f;    // Flow field cost weight [1, 8]
    uint32      OccupantID    = 0;        // VehicleAgent or StaffAgent ID; 0 = none

    bool IsStall()    const { return Type >= ETileType::Stall_Standard && Type <= ETileType::Stall_Disabled; }
    bool IsPassable() const { return !bBlocked && Type != ETileType::Wall && Type != ETileType::Empty; }
};

// ─────────────────────────────────────────────────────────────────
// VEHICLE
// ─────────────────────────────────────────────────────────────────

UENUM(BlueprintType)
enum class EVehicleType : uint8
{
    Compact,
    Standard,
    SUV,
    Motorcycle,
    Disabled,
    DeliveryVan     // Pure incident trigger — 0 revenue
};

UENUM(BlueprintType)
enum class EVehicleState : uint8
{
    Spawning,   // Entering from city
    Seeking,    // Following flow field toward stall
    Queuing,    // Waiting — congestion blocked
    Parking,    // Moving into stall
    Parked,     // Stationary, duration ticking
    Circling,   // No stall found after timeout
    Exiting,    // Following exit flow field
    Despawned
};

USTRUCT()
struct FVehicleData
{
    GENERATED_BODY()

    uint32        ID            = 0;
    EVehicleType  Type          = EVehicleType::Standard;
    EVehicleState State         = EVehicleState::Spawning;
    int32         FloorIndex    = 0;
    int32         CurrentTileID = -1;
    int32         TargetTileID  = -1;    // Stall tile reserved
    uint64        ParkStartTick = 0;
    uint64        DespawnTick   = 0;     // Drawn from seeded RNG on Parked entry
    float         SpeedModifier = 1.0f;
};

// ─────────────────────────────────────────────────────────────────
// STAFF
// ─────────────────────────────────────────────────────────────────

UENUM(BlueprintType)
enum class EStaffRole : uint8
{
    Attendant,
    Security,
    Maintenance
};

UENUM(BlueprintType)
enum class EStaffTrait : uint8
{
    SharpEyed,      // Detects incidents in adjacent tiles
    Diligent,       // Fatigue accumulates 30% slower
    EasilyBribed,   // Loyalty drops faster at high Shadow standing
    Slow,           // Movement speed -20%
    Experienced     // Resolves incidents 40% faster
};

UENUM(BlueprintType)
enum class EStaffTaskType : uint8
{
    Patrol,
    ResolveIncident,
    EscortVehicle,
    InvestigateTile,
    EmergencyRepair
};

USTRUCT()
struct FStaffTask
{
    GENERATED_BODY()

    EStaffTaskType  Type        = EStaffTaskType::Patrol;
    int32           TargetTileID = -1;
    uint32          IncidentID  = 0;      // If resolving incident
    int32           TicksRemaining = 0;
    bool            bInterruptible = true;
};

USTRUCT()
struct FStaffData
{
    GENERATED_BODY()

    uint32              ID            = 0;
    FString             Name;
    EStaffRole          Role          = EStaffRole::Attendant;
    EStaffTrait         Trait         = EStaffTrait::Diligent;
    float               Fatigue       = 0.0f;   // [0,1] — hidden from player
    float               Loyalty       = 1.0f;   // [0,1] — hidden from player
    int32               AssignedFloor = 0;
    int32               CurrentTileID = -1;
    TArray<FStaffTask>  TaskQueue;

    bool IsOnShift()         const { return true; }  // Expand with shift schedule later
    bool IsFatigued()        const { return Fatigue >= 0.8f; }
    float ResolutionSpeed()  const;  // Implemented in .cpp — factors Role + Trait + Fatigue
};

// ─────────────────────────────────────────────────────────────────
// INCIDENT
// ─────────────────────────────────────────────────────────────────

UENUM(BlueprintType)
enum class EIncidentType : uint8
{
    FenderBender,
    OilSpill,
    OverstayVehicle,
    PowerOutage,
    SuspiciousVehicle,
    MedicalEmergency,
    TheftInProgress,
    StructuralCrack,
    Altercation,
    PipeBurst
};

UENUM(BlueprintType)
enum class EIncidentState : uint8
{
    Pending,        // Spawned, not yet player-visible (may be in dark zone)
    Active,         // Player-visible, awaiting response
    DecisionPending,// Player must choose a branch
    InProgress,     // Staff dispatched, resolving
    Resolved,
    Expired         // Ignored past timer — default branch fired
};

USTRUCT(BlueprintType)
struct FIncidentData
{
    GENERATED_BODY()

    uint32          ID              = 0;
    EIncidentType   Type            = EIncidentType::FenderBender;
    EIncidentState  State           = EIncidentState::Pending;
    int32           FloorIndex      = 0;
    int32           TileID          = -1;
    float           Severity        = 0.5f;  // [0,1]
    uint64          SpawnTick       = 0;
    uint64          DecisionDeadlineTick = 0; // 0 = no decision required
    int32           AssignedStaffID = 0;      // 0 = unassigned
    float           SeverityAccumulator = 0.0f; // Grows while unresolved
};

// ─────────────────────────────────────────────────────────────────
// FACTION
// ─────────────────────────────────────────────────────────────────

UENUM(BlueprintType)
enum class EFactionType : uint8
{
    CityAuthority,
    Police,
    ShadowClients
};

USTRUCT(BlueprintType)
struct FFactionScores
{
    GENERATED_BODY()

    float CityAuthority  = 0.0f;   // [-100, 100]
    float Police         = 0.0f;
    float ShadowClients  = 0.0f;

    float Get(EFactionType Faction) const;
    void  Apply(EFactionType Faction, float Delta);
};

// ─────────────────────────────────────────────────────────────────
// CITY EVENT
// ─────────────────────────────────────────────────────────────────

UENUM(BlueprintType)
enum class ECityEventType : uint8
{
    StadiumNight,
    Rainstorm,
    PoliceSweep,
    ProtestBlock,
    ConcertLetout,
    CityInspection,
    PowerGridStrain,
    BlackMarketNight
};

USTRUCT(BlueprintType)
struct FCityEventData
{
    GENERATED_BODY()

    ECityEventType  Type;
    float           DemandMultiplier  = 1.0f;
    int32           DurationTicks     = 0;
    uint64          StartTick         = 0;
    bool            bIsActive         = false;
};
