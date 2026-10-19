extends RefCounted
## One parking floor: tile grid, occupancy, stall reservations, pathing,
## and structural integrity. Mirrors AParkingFloor in the UE build.
##
## Pathing uses Godot's native AStarGrid2D instead of the UE build's
## per-destination flow fields — same behavior for the playtest (shortest
## route around walls, stalls, blocked tiles and congestion), far cheaper
## to run in GDScript.

const C = preload("res://scripts/sim/sim_const.gd")

const NO_CELL := C.NO_CELL

var width := 0
var height := 0
var floor_index := 0

var types := PackedInt32Array()          # C.Tile per cell
var blocked := PackedByteArray()         # 1 = incident blocks this cell
var vehicle_count := PackedInt32Array()  # vehicles on lane/gate cells (stalls tracked by owner)
var stall_owner := PackedInt32Array()    # vehicle id holding the stall, 0 = free
var stalls: Array[Vector2i] = []
var unreachable_stalls := {}             # Vector2i -> true

var entry_gate := NO_CELL
var exit_gate := NO_CELL

var integrity := 1.0
var active_cracks := 0

var astar := AStarGrid2D.new()


# ── Indexing ─────────────────────────────────────────────────────

func idx(cell: Vector2i) -> int:
	return cell.y * width + cell.x


func in_bounds(cell: Vector2i) -> bool:
	return cell.x >= 0 and cell.y >= 0 and cell.x < width and cell.y < height


func tile_at(cell: Vector2i) -> int:
	return types[idx(cell)] if in_bounds(cell) else C.Tile.EMPTY


func is_drivable_type(tile_type: int) -> bool:
	return tile_type == C.Tile.LANE or tile_type == C.Tile.ENTRY_GATE \
		or tile_type == C.Tile.EXIT_GATE or tile_type == C.Tile.RAMP_UP \
		or tile_type == C.Tile.RAMP_DOWN


func is_blocked(cell: Vector2i) -> bool:
	return in_bounds(cell) and blocked[idx(cell)] == 1


func is_passable(cell: Vector2i) -> bool:
	return in_bounds(cell) and is_drivable_type(types[idx(cell)]) and blocked[idx(cell)] == 0


# ── Layout ───────────────────────────────────────────────────────
# Same structure as AParkingFloor::GenerateDefaultLayout, at any size:
#   row 0         wall, entry gate at W/4, exit gate at 3W/4
#   rows 1-2      lane corridor
#   rows 3..H-3   [stall][stall][lane] repeating
#   row H-2       lane corridor, row H-1 wall
#   cols 1-2 and W-3..W-2 vertical lanes, cols 0 and W-1 wall
# Stall columns (x = 3..W-4) are split left→right into compact, standard,
# oversized, motorcycle and disabled bands sized to the vehicle mix:
#   24×16 → 144 stalls (32/72/24/8/8)    16×10 → 40 stalls (8/16/8/4/4)

func generate_default_layout(w: int = 24, h: int = 16) -> void:
	width = w
	height = h
	var n := w * h
	types.resize(n)
	blocked.resize(n)
	vehicle_count.resize(n)
	stall_owner.resize(n)
	types.fill(C.Tile.EMPTY)
	blocked.fill(0)
	vehicle_count.fill(0)
	stall_owner.fill(0)
	stalls.clear()

	var bands := _stall_bands(w - 6)

	for y in h:
		for x in w:
			var cell := Vector2i(x, y)
			var t: int
			if y == 0 or y == h - 1 or x == 0 or x == w - 1:
				t = C.Tile.WALL
			else:
				var vertical_lane := x <= 2 or x >= w - 3
				var top_corridor := y <= 2
				var bottom_corridor := y == h - 2
				var lane_row := (y - 3) % 3 == 2
				if vertical_lane or top_corridor or bottom_corridor or lane_row:
					t = C.Tile.LANE
				else:
					t = bands[x - 3]
			types[idx(cell)] = t
			if C.is_stall(t):
				stalls.append(cell)

	entry_gate = Vector2i(clampi(w / 4, 1, w - 2), 0)
	exit_gate = Vector2i(clampi(3 * w / 4, 1, w - 2), 0)
	types[idx(entry_gate)] = C.Tile.ENTRY_GATE
	types[idx(exit_gate)] = C.Tile.EXIT_GATE

	_rebuild_astar()
	_compute_reachability()


## Stall type for each stall column, left to right. Shares follow the
## spawn mix in VehicleTypes.json: ~22% compact, ~18% oversized (SUVs and
## vans), one motorcycle and one disabled column, the rest standard.
func _stall_bands(columns: int) -> Array[int]:
	var compact := maxi(1, roundi(columns * 0.22))
	var oversized := maxi(1, roundi(columns * 0.18))
	var standard := maxi(1, columns - compact - oversized - 2)
	var out: Array[int] = []
	for _i in compact:
		out.append(C.Tile.STALL_COMPACT)
	for _i in standard:
		out.append(C.Tile.STALL_STANDARD)
	for _i in oversized:
		out.append(C.Tile.STALL_OVERSIZED)
	out.append(C.Tile.STALL_MOTORCYCLE)
	out.append(C.Tile.STALL_DISABLED)
	while out.size() < columns:   # tiny lots: pad with standard
		out.insert(compact, C.Tile.STALL_STANDARD)
	return out


func _rebuild_astar() -> void:
	astar = AStarGrid2D.new()
	astar.region = Rect2i(0, 0, width, height)
	astar.cell_size = Vector2(1, 1)
	astar.diagonal_mode = AStarGrid2D.DIAGONAL_MODE_NEVER
	astar.default_compute_heuristic = AStarGrid2D.HEURISTIC_MANHATTAN
	astar.default_estimate_heuristic = AStarGrid2D.HEURISTIC_MANHATTAN
	astar.update()
	for y in height:
		for x in width:
			var cell := Vector2i(x, y)
			astar.set_point_solid(cell, not is_passable(cell))


func _compute_reachability() -> void:
	unreachable_stalls.clear()
	for stall in stalls:
		var inbound := find_path(entry_gate, stall)
		var outbound := find_path(stall, exit_gate)
		if inbound.is_empty() or outbound.is_empty():
			unreachable_stalls[stall] = true


# ── Pathing ──────────────────────────────────────────────────────

## Shortest path from `from` to `to`, excluding `from` itself. Either end
## may be a stall or a blocked cell — those are opened for this query only,
## so the route can start or finish there but never passes through them.
## Returns an empty array when unreachable.
func find_path(from: Vector2i, to: Vector2i) -> Array[Vector2i]:
	var result: Array[Vector2i] = []
	if not in_bounds(from) or not in_bounds(to) or from == to:
		return result

	var from_solid := astar.is_point_solid(from)
	var to_solid := astar.is_point_solid(to)
	if from_solid:
		astar.set_point_solid(from, false)
	if to_solid:
		astar.set_point_solid(to, false)

	var raw := astar.get_id_path(from, to)

	if from_solid:
		astar.set_point_solid(from, true)
	if to_solid:
		astar.set_point_solid(to, true)

	for i in range(1, raw.size()):
		result.append(raw[i])
	return result


## Refreshes lane path weights from live vehicle counts so routes bend
## around queues (UE: TickIntegrity congestion cost update).
func update_congestion_weights() -> void:
	for y in height:
		for x in width:
			var cell := Vector2i(x, y)
			if types[idx(cell)] == C.Tile.LANE and blocked[idx(cell)] == 0:
				astar.set_point_weight_scale(cell, 1.0 + 1.5 * vehicle_count[idx(cell)])


func set_blocked(cell: Vector2i, value: bool) -> void:
	if not in_bounds(cell):
		return
	blocked[idx(cell)] = 1 if value else 0
	astar.set_point_solid(cell, not is_passable(cell))


# ── Occupancy ────────────────────────────────────────────────────

func add_vehicle(cell: Vector2i) -> void:
	if in_bounds(cell) and not C.is_stall(types[idx(cell)]):
		vehicle_count[idx(cell)] += 1


func remove_vehicle(cell: Vector2i) -> void:
	if in_bounds(cell) and not C.is_stall(types[idx(cell)]):
		vehicle_count[idx(cell)] = maxi(0, vehicle_count[idx(cell)] - 1)


func vehicles_on(cell: Vector2i) -> int:
	return vehicle_count[idx(cell)] if in_bounds(cell) else 0


func reserve_stall(cell: Vector2i, vehicle_id: int) -> void:
	stall_owner[idx(cell)] = vehicle_id


func release_stall(cell: Vector2i, vehicle_id: int) -> void:
	if in_bounds(cell) and stall_owner[idx(cell)] == vehicle_id:
		stall_owner[idx(cell)] = 0


## Nearest free stall of an allowed type by Manhattan distance.
## `exclude` holds stalls the caller already failed to route to.
func find_nearest_free_stall(from: Vector2i, allowed_types: Array, exclude: Dictionary = {}) -> Vector2i:
	var best := NO_CELL
	var best_dist := 1 << 30
	for stall in stalls:
		if stall_owner[idx(stall)] != 0 or unreachable_stalls.has(stall) or exclude.has(stall):
			continue
		if not allowed_types.has(types[idx(stall)]):
			continue
		var d := absi(stall.x - from.x) + absi(stall.y - from.y)
		if d < best_dist:
			best_dist = d
			best = stall
	return best


func count_free_stalls() -> int:
	var free := 0
	for stall in stalls:
		if stall_owner[idx(stall)] == 0:
			free += 1
	return free


func random_lane_cell(rng: RandomNumberGenerator) -> Vector2i:
	for _attempt in 32:
		var cell := Vector2i(rng.randi_range(1, width - 2), rng.randi_range(1, height - 2))
		if types[idx(cell)] == C.Tile.LANE and blocked[idx(cell)] == 0:
			return cell
	return NO_CELL


func passable_neighbors(cell: Vector2i) -> Array[Vector2i]:
	var out: Array[Vector2i] = []
	for d in [Vector2i.UP, Vector2i.DOWN, Vector2i.LEFT, Vector2i.RIGHT]:
		var n: Vector2i = cell + d
		if is_passable(n):
			out.append(n)
	return out


# ── Structural integrity (UE: AParkingFloor::TickIntegrity) ──────

func tick_integrity(idle_maintenance: int) -> void:
	integrity -= 0.000005
	integrity -= floor_index * 0.000002
	integrity -= active_cracks * 0.00005
	integrity += idle_maintenance * C.INTEGRITY_REPAIR_PER_IDLE_MAINT
	integrity = clampf(integrity, 0.0, 1.0)
