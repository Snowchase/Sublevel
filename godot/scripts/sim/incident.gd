extends RefCounted
## One incident instance. Definitions come from Data/IncidentTypes.json
## (see data_loader.gd); this holds the live state. Mirrors FIncidentData.

const C = preload("res://scripts/sim/sim_const.gd")

var id := 0
var type_name := ""
var def: Dictionary = {}
var state: int = C.IncidentState.ACTIVE

var cell := C.NO_CELL
var severity := 0.5
var blocking := false         # holds a lane tile closed while unresolved
var vehicle_id := 0           # parked vehicle this incident is about, 0 = none
var staff_id := 0             # staff member assigned, 0 = none

var spawn_tick := 0
var deadline_tick := 0        # decision deadline, 0 = no decision
var closed_tick := 0
var outcome := ""             # branch label or resolution note for the log


func requires_decision() -> bool:
	return bool(def.get("requires_decision", false))


func is_open() -> bool:
	return state == C.IncidentState.ACTIVE \
		or state == C.IncidentState.DECISION_PENDING \
		or state == C.IncidentState.IN_PROGRESS


func seconds_left(now_tick: int) -> float:
	if deadline_tick <= 0:
		return 0.0
	return maxf(0.0, (deadline_tick - now_tick) * C.TICK_SECONDS)
