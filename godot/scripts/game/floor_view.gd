extends Node2D
## Draws the parking floor, vehicles, staff and incidents with plain
## shapes (no art assets). Redrawn every frame from the sim state.

const C = preload("res://scripts/sim/sim_const.gd")
const Simulation = preload("res://scripts/sim/simulation.gd")

const TILE := 32.0

var sim: Simulation = null
var tick_frac := 0.0                # 0..1 progress toward the next sim tick
var hovered_cell := C.NO_CELL
var highlight_cells: Array[Vector2i] = []   # tutorial: tiles to point at
var _pulse := 0.0

const TILE_COLORS := {
	C.Tile.EMPTY: Color(0, 0, 0, 0),
	C.Tile.LANE: Color(0.10, 0.10, 0.12),
	C.Tile.STALL_STANDARD: Color(0.16, 0.22, 0.30),
	C.Tile.STALL_COMPACT: Color(0.14, 0.28, 0.26),
	C.Tile.STALL_OVERSIZED: Color(0.30, 0.22, 0.12),
	C.Tile.STALL_MOTORCYCLE: Color(0.24, 0.16, 0.30),
	C.Tile.STALL_DISABLED: Color(0.10, 0.25, 0.45),
	C.Tile.RAMP_UP: Color(0.40, 0.35, 0.10),
	C.Tile.RAMP_DOWN: Color(0.40, 0.35, 0.10),
	C.Tile.ENTRY_GATE: Color(0.10, 0.45, 0.12),
	C.Tile.EXIT_GATE: Color(0.45, 0.10, 0.10),
	C.Tile.WALL: Color(0.35, 0.35, 0.38),
}

const VEHICLE_COLORS := {
	"Compact": Color(0.30, 0.80, 0.80),
	"Standard": Color(0.78, 0.78, 0.84),
	"SUV": Color(0.90, 0.58, 0.20),
	"Motorcycle": Color(0.70, 0.40, 0.90),
	"Disabled": Color(0.30, 0.55, 1.00),
	"DeliveryVan": Color(0.95, 0.92, 0.25),
}

const ROLE_COLORS := [Color(0.95, 0.85, 0.25), Color(0.30, 0.50, 1.00), Color(1.00, 0.50, 0.15)]
const ROLE_LETTERS := ["A", "S", "M"]


func grid_pixel_size() -> Vector2:
	if sim == null:
		return Vector2.ZERO
	return Vector2(sim.grid.width, sim.grid.height) * TILE


func cell_center(cell: Vector2i) -> Vector2:
	return (Vector2(cell) + Vector2(0.5, 0.5)) * TILE


func cell_at(local_pos: Vector2) -> Vector2i:
	if sim == null:
		return C.NO_CELL
	var cell := Vector2i(floori(local_pos.x / TILE), floori(local_pos.y / TILE))
	return cell if sim.grid.in_bounds(cell) else C.NO_CELL


func _process(delta: float) -> void:
	_pulse = fmod(_pulse + delta * 3.0, TAU)
	hovered_cell = cell_at(get_local_mouse_position())
	queue_redraw()


func _draw() -> void:
	if sim == null:
		return
	_draw_tiles()
	_draw_incidents()
	_draw_vehicles()
	_draw_staff()
	_draw_outside_queue()
	_draw_highlights()
	_draw_hover()


func _draw_tiles() -> void:
	var grid = sim.grid
	var font := ThemeDB.fallback_font
	for y in grid.height:
		for x in grid.width:
			var cell := Vector2i(x, y)
			var t: int = grid.tile_at(cell)
			var rect := Rect2(Vector2(cell) * TILE, Vector2(TILE, TILE))
			if t == C.Tile.WALL:
				draw_rect(rect, TILE_COLORS[t])
				continue
			draw_rect(rect.grow(-1.0), TILE_COLORS.get(t, Color.BLACK))
			if t == C.Tile.STALL_MOTORCYCLE:
				draw_string(font, rect.position + Vector2(0, TILE * 0.62), "M", HORIZONTAL_ALIGNMENT_CENTER, TILE, 11, Color(1, 1, 1, 0.25))
			elif t == C.Tile.STALL_DISABLED:
				draw_string(font, rect.position + Vector2(0, TILE * 0.62), "D", HORIZONTAL_ALIGNMENT_CENTER, TILE, 11, Color(1, 1, 1, 0.25))
			if grid.is_blocked(cell):
				draw_rect(rect.grow(-3.0), Color(0.9, 0.2, 0.1, 0.35))

	draw_string(font, cell_center(grid.entry_gate) + Vector2(-TILE, 5), "IN", HORIZONTAL_ALIGNMENT_CENTER, TILE * 2, 13, Color.WHITE)
	draw_string(font, cell_center(grid.exit_gate) + Vector2(-TILE, 5), "OUT", HORIZONTAL_ALIGNMENT_CENTER, TILE * 2, 13, Color.WHITE)


func _draw_incidents() -> void:
	var font := ThemeDB.fallback_font
	var urgent = sim.next_decision()
	for inc in sim.incidents.values():
		if not inc.is_open():
			continue
		var center := cell_center(inc.cell)
		var color := Color(1.0, 0.55, 0.1)
		match inc.state:
			C.IncidentState.DECISION_PENDING:
				color = Color(1.0, 0.2, 0.2)
			C.IncidentState.IN_PROGRESS:
				color = Color(1.0, 0.85, 0.2)
		var r := TILE * (0.42 + 0.06 * sin(_pulse))
		draw_circle(center, r, Color(color, 0.25))
		draw_arc(center, r, 0, TAU, 24, color, 2.0)
		if inc == urgent:
			draw_arc(center, r + 5.0, 0, TAU, 24, Color(1, 1, 1, 0.8), 2.0)
		draw_string(font, center + Vector2(-TILE * 0.5, -TILE * 0.55), "!", HORIZONTAL_ALIGNMENT_CENTER, TILE, 16, color)


func _interp_pos(prev_cell: Vector2i, cell: Vector2i, cooldown: float, interval: float) -> Vector2:
	var progress := clampf(1.0 - (cooldown - tick_frac) / interval, 0.0, 1.0)
	return cell_center(prev_cell).lerp(cell_center(cell), progress)


func _draw_vehicles() -> void:
	for v in sim.vehicles.values():
		var pos := _interp_pos(v.prev_cell, v.cell, v.cooldown, v.move_interval)
		var color: Color = VEHICLE_COLORS.get(v.type_name, Color.WHITE)
		var heading: Vector2 = v.heading
		if v.is_parked():
			heading = Vector2.DOWN   # nose into the stall
		var length := TILE * 0.72
		var width := TILE * 0.42
		if v.type_name == "Motorcycle":
			width = TILE * 0.22
		elif v.type_name == "SUV" or v.type_name == "DeliveryVan":
			length = TILE * 0.82
			width = TILE * 0.5
		_draw_oriented_rect(pos, heading, length, width, color)
		if v.waiting and not v.is_parked():
			draw_circle(pos + heading.orthogonal() * width * 0.3, 3.0, Color(1, 0.25, 0.2))


func _draw_oriented_rect(center: Vector2, heading: Vector2, length: float, width: float, color: Color) -> void:
	var fwd := heading.normalized() if heading.length() > 0.01 else Vector2.RIGHT
	var side := fwd.orthogonal()
	var hl := fwd * length * 0.5
	var hw := side * width * 0.5
	var pts := PackedVector2Array([center - hl - hw, center + hl - hw, center + hl + hw, center - hl + hw])
	draw_colored_polygon(pts, color)
	# Windshield stripe shows direction of travel.
	draw_line(center + fwd * length * 0.2 - hw * 0.8, center + fwd * length * 0.2 + hw * 0.8, Color(0, 0, 0, 0.45), 2.0)


func _draw_staff() -> void:
	var font := ThemeDB.fallback_font
	for s in sim.staff.values():
		var pos := _interp_pos(s.prev_cell, s.cell, s.cooldown, s.move_interval)
		var color: Color = ROLE_COLORS[s.role]
		if s.is_fatigued():
			color = color.darkened(0.45)
		draw_circle(pos, TILE * 0.26, color)
		draw_arc(pos, TILE * 0.26, 0, TAU, 16, Color(0, 0, 0, 0.6), 1.5)
		draw_string(font, pos + Vector2(-8, 4), ROLE_LETTERS[s.role], HORIZONTAL_ALIGNMENT_CENTER, 16, 11, Color.BLACK)
		if not s.is_idle():
			draw_arc(pos, TILE * 0.34, -PI * 0.5, -PI * 0.5 + TAU * 0.75, 12, Color(1, 1, 1, 0.7), 1.5)


func _draw_outside_queue() -> void:
	var font := ThemeDB.fallback_font
	var gate := cell_center(sim.grid.entry_gate)
	var n: int = sim.outside_queue.size()
	for i in mini(n, 8):
		var item: Dictionary = sim.outside_queue[i]
		var pos := gate + Vector2(0, -TILE * (1.0 + i * 0.8))
		var color: Color = VEHICLE_COLORS.get(String(item["type"]), Color.WHITE)
		_draw_oriented_rect(pos, Vector2.DOWN, TILE * 0.6, TILE * 0.36, Color(color, 0.55))
	if n > 0:
		draw_string(font, gate + Vector2(TILE * 0.6, -TILE), "queue %d" % n, HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color(1, 0.8, 0.4))


## Tutorial pointer: pulsing gold frame plus a bouncing arrow above the tile.
func _draw_highlights() -> void:
	var glow := 0.55 + 0.45 * sin(_pulse * 1.5)
	var gold := Color(1.0, 0.85, 0.25, glow)
	for cell in highlight_cells:
		var rect := Rect2(Vector2(cell) * TILE, Vector2(TILE, TILE)).grow(3.0 + 2.0 * glow)
		draw_rect(rect, gold, false, 3.0)
		var tip := cell_center(cell) + Vector2(0, -TILE * (0.85 + 0.15 * sin(_pulse * 2.0)))
		draw_colored_polygon(PackedVector2Array([tip, tip + Vector2(-9, -14), tip + Vector2(9, -14)]), gold)


func _draw_hover() -> void:
	if hovered_cell == C.NO_CELL:
		return
	draw_rect(Rect2(Vector2(hovered_cell) * TILE, Vector2(TILE, TILE)), Color(1, 1, 1, 0.6), false, 1.5)


## One-line description of the hovered tile for the HUD.
func describe_hovered() -> String:
	if sim == null or hovered_cell == C.NO_CELL:
		return ""
	var grid = sim.grid
	var t: int = grid.tile_at(hovered_cell)
	var names := ["Empty", "Lane", "Standard stall", "Compact stall", "Oversized stall",
		"Motorcycle stall", "Disabled stall", "Ramp up", "Ramp down", "Entry gate", "Exit gate", "Wall"]
	var text := "(%d,%d) %s" % [hovered_cell.x, hovered_cell.y, names[t]]
	if C.is_stall(t):
		var owner_id: int = grid.stall_owner[grid.idx(hovered_cell)]
		if owner_id != 0 and sim.vehicles.has(owner_id):
			var v = sim.vehicles[owner_id]
			text += " — %s %s" % [v.type_name, C.VEHICLE_STATE_NAMES[v.state]]
			if v.is_parked():
				text += ", leaves %s" % C.tick_to_clock(v.depart_tick)
		elif owner_id == 0:
			text += " — free"
	elif t == C.Tile.LANE:
		text += " — %d car(s)" % grid.vehicles_on(hovered_cell)
	if grid.is_blocked(hovered_cell):
		text += " — BLOCKED"
	for inc in sim.incidents.values():
		if inc.is_open() and inc.cell == hovered_cell:
			text += " — incident: %s" % inc.type_name
	return text
