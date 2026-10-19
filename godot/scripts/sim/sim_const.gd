extends RefCounted
## Shared enums, clock constants and playtest tuning for the SUBLEVEL sim.
## Mirrors Source/SUBLEVEL/SubLevelTypes.h where an equivalent exists.
## Preload this script as `C` — no class_name, so headless runs work
## without an editor import pass.

# ── Clock (same as SubLevelSim:: in the UE build) ────────────────
const TICK_SECONDS := 0.05          # 20Hz sim tick
const TICKS_PER_DAY := 14400        # one in-game day ≈ 12 real minutes at ×1
const TICKS_PER_HOUR := 600         # one in-game hour = 30 real seconds at ×1
const START_HOUR := 6               # playtest sessions start at 06:00, not midnight

# ── Tiles ────────────────────────────────────────────────────────
enum Tile { EMPTY, LANE, STALL_STANDARD, STALL_COMPACT, STALL_OVERSIZED,
	STALL_MOTORCYCLE, STALL_DISABLED, RAMP_UP, RAMP_DOWN, ENTRY_GATE, EXIT_GATE, WALL }

const NO_CELL := Vector2i(-1, -1)

# ── Vehicles ─────────────────────────────────────────────────────
# SEEKING: driving to a reserved stall (or waiting for one to free up)
# LEAVING_UNSERVED: gave up finding a stall, driving out with no fee (UE: Circling)
enum VehicleState { SEEKING, PARKED, EXITING, LEAVING_UNSERVED, DONE }
const VEHICLE_STATE_NAMES := ["seeking", "parked", "exiting", "leaving", "done"]

const VEHICLE_TYPES := ["Compact", "Standard", "SUV", "Motorcycle", "Disabled", "DeliveryVan"]

# Stall types each vehicle type may use (VehicleTypes.json "stall_requirements").
const DEFAULT_STALL_RULES := {
	"Compact": [Tile.STALL_COMPACT, Tile.STALL_STANDARD],
	"Standard": [Tile.STALL_STANDARD],
	"SUV": [Tile.STALL_OVERSIZED],
	"Motorcycle": [Tile.STALL_MOTORCYCLE],
	"Disabled": [Tile.STALL_DISABLED],
	"DeliveryVan": [Tile.STALL_OVERSIZED],
}

# ── Staff ────────────────────────────────────────────────────────
enum Role { ATTENDANT, SECURITY, MAINTENANCE }
const ROLE_NAMES := ["Attendant", "Security", "Maintenance"]

enum Trait { SHARP_EYED, DILIGENT, EASILY_BRIBED, SLOW, EXPERIENCED }
const TRAIT_NAMES := ["Sharp-eyed", "Diligent", "Easily bribed", "Slow", "Experienced"]

const STAFF_WAGES := [80.0, 120.0, 100.0]   # per day, indexed by Role

# ── Incidents ────────────────────────────────────────────────────
enum IncidentState { ACTIVE, DECISION_PENDING, IN_PROGRESS, RESOLVED, EXPIRED }

# Incident types that physically block the lane tile they sit on.
const BLOCKING_INCIDENTS := ["FenderBender", "OilSpill", "MedicalEmergency",
	"Altercation", "StructuralCrack", "PipeBurst"]

# Incident types that attach to a parked vehicle when one exists.
const VEHICLE_INCIDENTS := ["OverstayVehicle", "SuspiciousVehicle", "TheftInProgress"]

# Money effect of decision-branch consequence tags (UE: ApplyConsequence).
const CONSEQUENCE_MONEY := {
	"vehicle_towed_revenue_recovered": 75.0,
	"fine_issued_vehicle_stays": 50.0,
	"vehicle_stays_shadow_credit": 100.0,
	"theft_completed_revenue_lost": -200.0,
	"emergency_services_arrive": -25.0,
}
# Consequence tags that remove the incident's vehicle from the lot.
const TOW_CONSEQUENCES := ["vehicle_towed_revenue_recovered", "vehicle_towed_police_arrive"]

# ── Factions ─────────────────────────────────────────────────────
const FACTIONS := ["city_authority", "police", "shadow_clients"]
const FACTION_LABELS := {"city_authority": "City", "police": "Police", "shadow_clients": "Shadow"}

# ── Playtest tuning ──────────────────────────────────────────────
# Values marked (UE) match the Unreal build. Others are playtest-only
# hypotheses — they are the knobs this playtest is meant to evaluate.
const STARTING_BALANCE := 10000.0               # (UE)
const HOURLY_RATE := 4.0                        # (UE) $ per in-game hour
const MIN_BILLED_HOURS := 0.25                  # (UE) 15-minute minimum
const SURGE_MULTIPLIER := 1.5                   # (UE)
const LOAN_AMOUNT := 5000.0                     # one press of the loan key
const LOAN_INTEREST_RATE := 0.02                # (UE) daily, on principal
const LOAN_REPAYMENT_FRACTION := 0.05           # (UE) of principal, daily
const BANKRUPT_THRESHOLD := -5000.0             # (UE) default threshold
const SHADOW_INCOME_PER_TICK := 0.05            # (UE) during BlackMarketNight

const VEHICLE_TICKS_PER_TILE := 4.0             # (UE)
const STAFF_TICKS_PER_TILE := 6.0               # (UE)
const MAX_VEHICLES_PER_TILE := 2                # (UE) congestion cap
const SEEK_TIMEOUT_TICKS := 200                 # (UE) no stall → give up
const QUEUE_REPATH_TICKS := 60                  # blocked this long → re-route
const OUTSIDE_QUEUE_MAX := 8                    # cars waiting on the street
const OUTSIDE_QUEUE_PATIENCE := 300             # ticks before a queued car leaves
const CONGESTION_UPDATE_TICKS := 20             # path weights refresh (1s)

const STAFF_WORK_TICKS := 200.0                 # (UE) base on-site work time
const AUTO_INCIDENT_TIMEOUT := 2400             # unattended 2 in-game hours → expires
const AUTO_INCIDENT_EXPIRE_FINE := 50.0
const INSPECTION_FINE := 500.0
const INTEGRITY_REPAIR_PER_IDLE_MAINT := 0.000004

const GOAL_DAYS := 7                            # playtest goal: survive 7 days


static func tick_to_hour(tick: int) -> int:
	return int((tick % TICKS_PER_DAY) / TICKS_PER_HOUR)


static func tick_to_clock(tick: int) -> String:
	var in_day := tick % TICKS_PER_DAY
	var hour := int(in_day / TICKS_PER_HOUR)
	var minute := int((in_day % TICKS_PER_HOUR) * 60 / TICKS_PER_HOUR)
	return "%02d:%02d" % [hour, minute]


static func is_stall(tile_type: int) -> bool:
	return tile_type >= Tile.STALL_STANDARD and tile_type <= Tile.STALL_DISABLED


static func tile_from_name(name: String) -> int:
	match name:
		"Stall_Standard": return Tile.STALL_STANDARD
		"Stall_Compact": return Tile.STALL_COMPACT
		"Stall_Oversized": return Tile.STALL_OVERSIZED
		"Stall_Motorcycle": return Tile.STALL_MOTORCYCLE
		"Stall_Disabled": return Tile.STALL_DISABLED
	return -1
