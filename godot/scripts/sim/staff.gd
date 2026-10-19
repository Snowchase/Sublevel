extends RefCounted
## One employee. Mirrors AStaffAgent: a single task slot (resolve an
## incident or respond to a decision), patrol wandering when idle, and
## hidden fatigue/loyalty stats with trait modifiers.

const C = preload("res://scripts/sim/sim_const.gd")

var id := 0
var role: int = C.Role.ATTENDANT
var trait_id: int = C.Trait.DILIGENT
var display_name := ""

var cell := Vector2i.ZERO
var prev_cell := Vector2i.ZERO
var path: Array[Vector2i] = []
var cooldown := 0.0
var move_interval := C.STAFF_TICKS_PER_TILE

var fatigue := 0.0            # [0,1] hidden from the player
var loyalty := 1.0            # [0,1] hidden from the player
var fatigue_notified := false
var compromised_notified := false

# Task: {} when idle, else {"kind": "resolve"|"respond", "incident_id": int,
#        "target": Vector2i, "work_left": int}
var task: Dictionary = {}
var patrol_target := C.NO_CELL
var next_retarget_tick := 0


func setup(staff_id: int, staff_role: int, staff_trait: int, start_cell: Vector2i) -> void:
	id = staff_id
	role = staff_role
	trait_id = staff_trait
	cell = start_cell
	prev_cell = start_cell
	display_name = "%s #%d" % [C.ROLE_NAMES[role], id]
	if trait_id == C.Trait.SLOW:
		move_interval = C.STAFF_TICKS_PER_TILE / 0.8


func is_idle() -> bool:
	return task.is_empty()


func is_fatigued() -> bool:
	return fatigue >= 0.8


## UE: FStaffData::ResolutionSpeed
func resolution_speed() -> float:
	var speed := 1.0
	if trait_id == C.Trait.EXPERIENCED:
		speed *= 1.4
	if trait_id == C.Trait.SLOW:
		speed *= 0.8
	return speed * lerpf(1.0, 0.5, fatigue)


func move_progress() -> float:
	return clampf(1.0 - cooldown / move_interval, 0.0, 1.0)


func assign(kind: String, incident_id: int, target: Vector2i, work_ticks: int) -> void:
	task = {"kind": kind, "incident_id": incident_id, "target": target, "work_left": work_ticks}
	path.clear()


func tick(sim) -> void:
	if task.is_empty():
		_patrol(sim)
		_tick_stats(sim, false)
	else:
		_work(sim)
		_tick_stats(sim, true)


func _work(sim) -> void:
	var incident = sim.incidents.get(task["incident_id"])
	if task["kind"] == "resolve" and (incident == null or not incident.is_open()):
		task = {}
		path.clear()
		return

	var target: Vector2i = task["target"]
	if cell != target:
		if path.is_empty():
			path = sim.grid.find_path(cell, target)
			if path.is_empty():
				return   # no route right now — try again next tick
		_walk()
		return

	if task["work_left"] > 0:
		task["work_left"] -= 1
		return

	if task["kind"] == "resolve":
		sim.resolve_by_staff(task["incident_id"], self)
	task = {}


func _patrol(sim) -> void:
	var grid = sim.grid
	if patrol_target == C.NO_CELL or cell == patrol_target or sim.tick_count >= next_retarget_tick:
		patrol_target = grid.random_lane_cell(sim.rng)
		next_retarget_tick = sim.tick_count + 200
		path = grid.find_path(cell, patrol_target) if patrol_target != C.NO_CELL else path
	if not path.is_empty():
		_walk()


func _walk() -> void:
	cooldown -= 1.0
	if cooldown > 0.0 or path.is_empty():
		return
	prev_cell = cell
	cell = path[0]
	path.remove_at(0)
	cooldown += move_interval


func _tick_stats(sim, working: bool) -> void:
	var rate := 0.0005 if working else -0.0002
	if working and trait_id == C.Trait.DILIGENT:
		rate *= 0.7
	fatigue = clampf(fatigue + rate, 0.0, 1.0)

	if fatigue >= 0.8 and not fatigue_notified:
		fatigue_notified = true
		sim.log_message("%s looks exhausted." % display_name, "warn")
	elif fatigue < 0.6:
		fatigue_notified = false

	if trait_id == C.Trait.EASILY_BRIBED and sim.factions["shadow_clients"] >= 20.0:
		loyalty = clampf(loyalty - 0.0001, 0.0, 1.0)
		if loyalty <= 0.0 and not compromised_notified:
			compromised_notified = true
			sim.log_message("%s has been seen talking with shadow clients." % display_name, "bad")
