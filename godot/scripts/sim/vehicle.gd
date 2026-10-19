extends RefCounted
## One customer vehicle. Mirrors AVehicleAgent's FSM:
##   SEEKING  → reserve nearest compatible stall, drive to it
##   PARKED   → wait out a duration drawn from VehicleTypes.json
##   EXITING  → drive to the exit gate, pay on arrival
##   LEAVING_UNSERVED → no stall found in time, drive out without paying
## `sim` is the owning simulation.gd instance (untyped to avoid a
## preload cycle).

const C = preload("res://scripts/sim/sim_const.gd")

var id := 0
var type_name := "Standard"
var def: Dictionary = {}
var state: int = C.VehicleState.SEEKING

var cell := Vector2i.ZERO
var prev_cell := Vector2i.ZERO
var heading := Vector2.RIGHT
var path: Array[Vector2i] = []

var stall := C.NO_CELL
var failed_stalls := {}          # stalls we could not route to this seek

var spawn_tick := 0
var park_start := 0
var depart_tick := 0
var last_move_tick := 0

var move_interval := C.VEHICLE_TICKS_PER_TILE
var cooldown := 0.0
var wait_ticks := 0              # blocked ticks since the last re-route attempt
var held_total := 0              # consecutive blocked ticks since the last move
var seek_ticks := 0              # ticks spent without a reachable stall
var waiting := false             # true while held up by congestion/blockage


func setup(vehicle_id: int, vehicle_def: Dictionary, start_cell: Vector2i, tick: int) -> void:
	id = vehicle_id
	def = vehicle_def
	type_name = String(def.get("type", "Standard"))
	cell = start_cell
	prev_cell = start_cell
	spawn_tick = tick
	last_move_tick = tick
	var speed := maxf(float(def.get("speed", 1.0)), 0.1)
	move_interval = C.VEHICLE_TICKS_PER_TILE / speed


## Visual interpolation 0..1 between prev_cell and cell.
func move_progress() -> float:
	return clampf(1.0 - cooldown / move_interval, 0.0, 1.0)


func is_parked() -> bool:
	return state == C.VehicleState.PARKED


func tick(sim) -> void:
	match state:
		C.VehicleState.SEEKING:
			_tick_seeking(sim)
		C.VehicleState.PARKED:
			if sim.tick_count >= depart_tick:
				begin_exit(sim)
		C.VehicleState.EXITING, C.VehicleState.LEAVING_UNSERVED:
			_tick_driving_out(sim)


# ── Seeking ──────────────────────────────────────────────────────

func _tick_seeking(sim) -> void:
	var grid = sim.grid

	if stall == C.NO_CELL:
		# Try a handful of candidates; skip any we cannot route to right now
		# (e.g. an incident is blocking the only lane in front of it).
		for _attempt in 4:
			var candidate: Vector2i = grid.find_nearest_free_stall(cell, def.get("stalls", []), failed_stalls)
			if candidate == C.NO_CELL:
				break
			var route: Array[Vector2i] = grid.find_path(cell, candidate)
			if route.is_empty():
				failed_stalls[candidate] = true
				continue
			stall = candidate
			path = route
			grid.reserve_stall(stall, id)
			break

		if stall == C.NO_CELL:
			seek_ticks += 1
			waiting = true
			if seek_ticks % 40 == 0:
				failed_stalls.clear()   # blockages clear — look again
			if seek_ticks >= C.SEEK_TIMEOUT_TICKS:
				leave_unserved(sim)
			return

	if _advance(sim, stall):
		state = C.VehicleState.PARKED
		waiting = false
		park_start = sim.tick_count
		depart_tick = park_start + sim.rng.randi_range(int(def.get("park_min", 400)), int(def.get("park_max", 4800)))
		sim.on_vehicle_parked(self)
		return

	# Stuck behind a blockage too long — give the stall back and look again.
	if held_total >= C.SEEK_TIMEOUT_TICKS:
		grid.release_stall(stall, id)
		failed_stalls[stall] = true
		stall = C.NO_CELL
		path.clear()
		held_total = 0


# ── Leaving ──────────────────────────────────────────────────────

func begin_exit(sim) -> void:
	state = C.VehicleState.EXITING
	path = sim.grid.find_path(cell, sim.grid.exit_gate)
	cooldown = 0.0
	last_move_tick = sim.tick_count   # parked time isn't "stuck" time


func leave_unserved(sim) -> void:
	if stall != C.NO_CELL:
		sim.grid.release_stall(stall, id)
		stall = C.NO_CELL
	state = C.VehicleState.LEAVING_UNSERVED
	path = sim.grid.find_path(cell, sim.grid.exit_gate)
	last_move_tick = sim.tick_count


func _tick_driving_out(sim) -> void:
	var grid = sim.grid
	if cell == grid.exit_gate:
		_finish(sim)
		return
	if _advance(sim, grid.exit_gate):
		_finish(sim)


func _finish(sim) -> void:
	sim.grid.remove_vehicle(cell)
	if state == C.VehicleState.EXITING:
		sim.on_vehicle_exited(self)
	else:
		sim.on_vehicle_left_unserved(self)
	state = C.VehicleState.DONE


## Removes the vehicle immediately (towed by a decision branch).
func tow(sim) -> void:
	if stall != C.NO_CELL:
		sim.grid.release_stall(stall, id)
		stall = C.NO_CELL
	sim.grid.remove_vehicle(cell)
	state = C.VehicleState.DONE


# ── Movement ─────────────────────────────────────────────────────

## Steps one tile along `path` when the move cooldown allows.
## Re-routes toward `destination` if the path is blocked or jammed too long.
## Returns true once the vehicle stands on `destination`.
func _advance(sim, destination: Vector2i) -> bool:
	if cell == destination:
		return true

	cooldown -= 1.0
	if cooldown > 0.0:
		return false

	var grid = sim.grid
	if path.is_empty():
		path = grid.find_path(cell, destination)
		if path.is_empty():
			_hold(sim, destination)
			return false

	var next: Vector2i = path[0]
	var into_stall: bool = C.is_stall(grid.tile_at(next))

	if not into_stall:
		if grid.is_blocked(next):
			path = grid.find_path(cell, destination)
			_hold(sim, destination)
			return false
		if grid.vehicles_on(next) >= C.MAX_VEHICLES_PER_TILE:
			_hold(sim, destination)
			return false

	# Move.
	grid.remove_vehicle(cell)
	prev_cell = cell
	cell = next
	grid.add_vehicle(cell)
	path.remove_at(0)

	heading = Vector2(cell - prev_cell)
	cooldown += move_interval    # cooldown is in (-1, 0] here; carry the fraction
	wait_ticks = 0
	held_total = 0
	waiting = false
	last_move_tick = sim.tick_count

	# Leaving our stall frees it for the next customer.
	if prev_cell == stall and state != C.VehicleState.SEEKING:
		grid.release_stall(stall, id)
		stall = C.NO_CELL

	return cell == destination


func _hold(sim, destination: Vector2i) -> void:
	cooldown = 0.0
	waiting = true
	wait_ticks += 1
	held_total += 1
	if wait_ticks >= C.QUEUE_REPATH_TICKS:
		wait_ticks = 0
		path = sim.grid.find_path(cell, destination)
