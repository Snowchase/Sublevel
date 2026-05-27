#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SubLevelTypes.h"
#include "EventBusSubsystem.generated.h"

// ─────────────────────────────────────────────────────────────────
// DELEGATE DECLARATIONS
// All inter-module communication routes through these delegates.
// Modules subscribe here — never call each other directly.
// ─────────────────────────────────────────────────────────────────

// Vehicle lifecycle
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVehicleArrived,   uint32,          VehicleID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVehicleParked,   uint32,          VehicleID,   int32, StallTileID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVehicleExited,   uint32,          VehicleID,   float, Revenue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVehicleDespawned, uint32,          VehicleID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCongestionDetected, int32,        FloorIndex,  int32, TileID);

// Incident lifecycle
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIncidentSpawned,  FIncidentData,   Incident);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIncidentVisible,  FIncidentData,   Incident);   // Entered visible zone
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDecisionRequired, FIncidentData,   Incident);   // UI must surface decision card
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDecisionMade,    uint32,          IncidentID,  int32, BranchIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIncidentResolved, uint32,          IncidentID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIncidentExpired,  uint32,          IncidentID); // Ignored past timer

// Floor / structure
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFloorPowerLost,     int32,         FloorIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFloorPowerRestored, int32,         FloorIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnIntegrityChanged,  int32,         FloorIndex,  float, NewIntegrity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFloorCollapse,      int32,         FloorIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFloorUnlocked,      int32,         FloorIndex);

// Staff (behavioral signals only — no stat values broadcast)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStaffFatigued,   uint32,          StaffID);    // Crossed 0.8 threshold
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStaffCompromised, uint32,         StaffID);    // Loyalty 0 — bribable
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStaffTaskChanged, uint32,        StaffID,     EStaffTaskType, NewTask);

// City / factions
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCityEventStarted, FCityEventData, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCityEventEnded,   FCityEventData, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFactionChanged,  EFactionType,   Faction,     float, NewScore);

// Economy
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRevenueEarned,    float,          Amount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoanRepaymentDue, float,          Amount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoanDefaulted);

// Camera / CCTV tab mode
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCCTVAlertPip,     int32,          CameraID);   // Unreviewed incident in zone

// ─────────────────────────────────────────────────────────────────
// SUBSYSTEM
// ─────────────────────────────────────────────────────────────────

UCLASS()
class SUBLEVEL_API UEventBusSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:

    // ── Vehicle ──────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable) FOnVehicleArrived     OnVehicleArrived;
    UPROPERTY(BlueprintAssignable) FOnVehicleParked      OnVehicleParked;
    UPROPERTY(BlueprintAssignable) FOnVehicleExited      OnVehicleExited;
    UPROPERTY(BlueprintAssignable) FOnVehicleDespawned   OnVehicleDespawned;
    UPROPERTY(BlueprintAssignable) FOnCongestionDetected OnCongestionDetected;

    // ── Incident ─────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable) FOnIncidentSpawned    OnIncidentSpawned;
    UPROPERTY(BlueprintAssignable) FOnIncidentVisible    OnIncidentVisible;
    UPROPERTY(BlueprintAssignable) FOnDecisionRequired   OnDecisionRequired;
    UPROPERTY(BlueprintAssignable) FOnDecisionMade       OnDecisionMade;
    UPROPERTY(BlueprintAssignable) FOnIncidentResolved   OnIncidentResolved;
    UPROPERTY(BlueprintAssignable) FOnIncidentExpired    OnIncidentExpired;

    // ── Floor ────────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable) FOnFloorPowerLost     OnFloorPowerLost;
    UPROPERTY(BlueprintAssignable) FOnFloorPowerRestored OnFloorPowerRestored;
    UPROPERTY(BlueprintAssignable) FOnIntegrityChanged   OnIntegrityChanged;
    UPROPERTY(BlueprintAssignable) FOnFloorCollapse      OnFloorCollapse;
    UPROPERTY(BlueprintAssignable) FOnFloorUnlocked      OnFloorUnlocked;

    // ── Staff ────────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable) FOnStaffFatigued      OnStaffFatigued;
    UPROPERTY(BlueprintAssignable) FOnStaffCompromised   OnStaffCompromised;
    UPROPERTY(BlueprintAssignable) FOnStaffTaskChanged   OnStaffTaskChanged;

    // ── City ─────────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable) FOnCityEventStarted   OnCityEventStarted;
    UPROPERTY(BlueprintAssignable) FOnCityEventEnded     OnCityEventEnded;
    UPROPERTY(BlueprintAssignable) FOnFactionChanged     OnFactionChanged;

    // ── Economy ──────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable) FOnRevenueEarned      OnRevenueEarned;
    UPROPERTY(BlueprintAssignable) FOnLoanRepaymentDue   OnLoanRepaymentDue;
    UPROPERTY(BlueprintAssignable) FOnLoanDefaulted      OnLoanDefaulted;

    // ── CCTV ─────────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable) FOnCCTVAlertPip       OnCCTVAlertPip;

    // ── Helper ───────────────────────────────────────────────────
    // Static getter so any system can access the bus without a reference chain
    static UEventBusSubsystem* Get(const UObject* WorldContext);
};
