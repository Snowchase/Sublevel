extends Node2D
## Draws the parking floor, vehicles, staff and incidents. Uses the
## generated art in res://assets/ when it's there (see art.gd), and falls
## back to plain shapes for anything missing. Redrawn every frame.

const C = preload("res://scripts/sim/sim_const.gd")
const Simulation = preload("res://scripts/sim/simulation.gd")
const Art = preload("res://scripts/game/art.gd")

# Sprite length along the direction of travel, in tiles.
const VEHICLE_LENGTH := {
	"Compact": 0.70, "Standard": 0.80, "SUV": 0.86,
	"Disabled": 0.84, "DeliveryVan": 0.88, "Motorcycle": 0.52,
}
const TEXTURE_TILES := 4          # one floor texture spans 4x4 tiles
const SHADOW := Color(0, 0, 0, 0.35)

# Incidents that leave a mark on the floor under their icon.
const INCIDENT_DECALS := {"OilSpill": "oil_stain", "FenderBender": "oil_stain", "StructuralCrack": "crack", "PipeBurst": "oil_stain"}

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
	if Art.has_floor_art():
		_draw_tiles_art()
		return
	var grid = sim.grid
	var font := ThemeDB.fallback_font
	for y in grid.height:
		for x in grid.width:
			var cell := Vector2i(x, y)
			var t: int = grid.tile_at(cell)
			var rect := Rect2(Vector2(cell) * TILE, Vector2(TILE, TILE))
			if grid.is_locked(cell):
				_draw_locked(rect, null)
				continue
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
	_draw_expansion_sign()


func _draw_incidents() -> void:
	var font := ThemeDB.fallback_font
	var urgent = sim.next_decision()
	for inc in sim.incidents.values():
		if not inc.is_open():
			continue
		var icon := Art.incident_icon(inc.type_name)
		if icon != null:
			_draw_incident_art(inc, icon, inc == urgent)
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
		var sprite := Art.vehicle(v.type_name)
		if sprite != null:
			_draw_sprite(sprite, pos, heading.angle(), TILE * float(VEHICLE_LENGTH.get(v.type_name, 0.8)), true)
			if v.waiting and not v.is_parked():
				draw_circle(pos + Vector2(0, -TILE * 0.38), 3.5, Color(1, 0.25, 0.2))
			continue
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
		var sprite := Art.staff(s.role)
		if sprite != null:
			# Role ring keeps roles readable at a glance; the sprite faces where it walks.
			draw_circle(pos, TILE * 0.33, Color(color, 0.35))
			draw_arc(pos, TILE * 0.33, 0, TAU, 20, color, 1.5)
			var step: Vector2 = Vector2(s.cell - s.prev_cell)
			var facing := Vector2.DOWN if step == Vector2.ZERO else step
			var tint := Color(0.55, 0.55, 0.6) if s.is_fatigued() else Color.WHITE
			_draw_sprite(sprite, pos, facing.angle() - PI * 0.5, TILE * 0.62, false, tint)
			if not s.is_idle():
				draw_arc(pos, TILE * 0.40, -PI * 0.5, -PI * 0.5 + TAU * 0.75, 14, Color(1, 1, 1, 0.8), 1.5)
			continue
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
		var type_name := String(item["type"])
		var sprite := Art.vehicle(type_name)
		if sprite != null:
			_draw_sprite(sprite, pos, PI * 0.5, TILE * 0.6, false, Color(1, 1, 1, 0.6))
			continue
		var color: Color = VEHICLE_COLORS.get(type_name, Color.WHITE)
		_draw_oriented_rect(pos, Vector2.DOWN, TILE * 0.6, TILE * 0.36, Color(color, 0.55))
	if n > 0:
		draw_string(font, gate + Vector2(TILE * 0.6, -TILE), "queue %d" % n, HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color(1, 0.8, 0.4))


# ── Art mode ─────────────────────────────────────────────────────

## Draws a texture centred on `pos`, rotated by `angle`, scaled so its
## width (the sprite's long axis) is `length` px; optional soft shadow.
func _draw_sprite(sprite: Texture2D, pos: Vector2, angle: float, length: float,
		shadow: bool, tint: Color = Color.WHITE) -> void:
	var size := Vector2(length, length * float(sprite.get_height()) / float(sprite.get_width()))
	var rect := Rect2(-size * 0.5, size)
	if shadow:
		draw_set_transform(pos + Vector2(1.5, 2.5), angle, Vector2.ONE)
		draw_texture_rect(sprite, rect, false, SHADOW)
	draw_set_transform(pos, angle, Vector2.ONE)
	draw_texture_rect(sprite, rect, false, tint)
	draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)


## Draws the part of a 4x4-tile texture that belongs to this cell, so a
## seamless texture flows across tiles instead of repeating every tile.
func _draw_floor(texture: Texture2D, cell: Vector2i, tint: Color = Color.WHITE) -> void:
	var span := float(texture.get_width()) / TEXTURE_TILES
	var region := Rect2(Vector2(posmod(cell.x, TEXTURE_TILES), posmod(cell.y, TEXTURE_TILES)) * span, Vector2(span, span))
	draw_texture_rect_region(texture, Rect2(Vector2(cell) * TILE, Vector2(TILE, TILE)), region, tint)


func _draw_tiles_art() -> void:
	var grid = sim.grid
	var font := ThemeDB.fallback_font
	var concrete := Art.tile("concrete")
	var asphalt := Art.tile("asphalt")
	var wall := Art.tile("wall")
	for y in grid.height:
		for x in grid.width:
			var cell := Vector2i(x, y)
			var t: int = grid.tile_at(cell)
			var rect := Rect2(Vector2(cell) * TILE, Vector2(TILE, TILE))
			if t == C.Tile.EMPTY:
				continue
			if grid.is_locked(cell):
				_draw_locked(rect, concrete)
				continue
			if t == C.Tile.WALL:
				# The wall texture has its hazard stripe on the bottom edge; turn
				# each wall so the stripe faces into the garage.
				var angle := 0.0
				if y == grid.height - 1:
					angle = PI
				elif x == 0 and y > 0:
					angle = -PI * 0.5
				elif x == grid.width - 1 and y > 0:
					angle = PI * 0.5
				draw_set_transform(rect.get_center(), angle, Vector2.ONE)
				draw_texture_rect(wall, Rect2(Vector2(-TILE, -TILE) * 0.5, Vector2(TILE, TILE)), false, Color(0.8, 0.8, 0.85))
				draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
				continue
			if C.is_stall(t):
				_draw_floor(concrete, cell, Color(0.82, 0.82, 0.86))
				# Keep the stall-type colour coding as a translucent wash.
				draw_rect(rect, Color(TILE_COLORS[t].lightened(0.1), 0.45))
				# Painted bay lines on the sides.
				var line := Color(0.92, 0.92, 0.88, 0.75)
				draw_line(rect.position, rect.position + Vector2(0, TILE), line, 1.5)
				draw_line(rect.position + Vector2(TILE, 0), rect.end, line, 1.5)
				if t == C.Tile.STALL_MOTORCYCLE:
					draw_string(font, rect.position + Vector2(0, TILE * 0.62), "M", HORIZONTAL_ALIGNMENT_CENTER, TILE, 11, Color(1, 1, 1, 0.45))
				elif t == C.Tile.STALL_DISABLED:
					draw_string(font, rect.position + Vector2(0, TILE * 0.62), "D", HORIZONTAL_ALIGNMENT_CENTER, TILE, 11, Color(1, 1, 1, 0.45))
			else:
				_draw_floor(asphalt, cell, Color(0.85, 0.85, 0.9))
			if grid.is_blocked(cell):
				draw_rect(rect.grow(-2.0), Color(0.9, 0.2, 0.1, 0.25))

	_draw_gate(grid.entry_gate, "IN", Color(0.25, 0.85, 0.35))
	_draw_gate(grid.exit_gate, "OUT", Color(0.95, 0.3, 0.3))
	_draw_expansion_sign()


## Unbuilt floor: bare dark concrete with faint construction-tape stripes.
func _draw_locked(rect: Rect2, concrete: Texture2D) -> void:
	if concrete != null:
		draw_texture_rect(concrete, rect, false, Color(0.26, 0.26, 0.29))
	else:
		draw_rect(rect, Color(0.07, 0.07, 0.08))
	var tape := Color(0.95, 0.78, 0.2, 0.10)
	draw_line(rect.position, rect.end, tape, 2.0)
	draw_line(rect.position + Vector2(rect.size.x * 0.5, 0), rect.position + Vector2(rect.size.x, rect.size.y * 0.5), tape, 2.0)


## Sign over the next unbuilt section: what expanding gets you and costs.
func _draw_expansion_sign() -> void:
	var grid = sim.grid
	if not grid.can_expand():
		return
	var rows: Array = grid.sections[grid.built_sections]
	var mid_y := (float(rows[0]) + float(rows[1]) + 1.0) * 0.5 * TILE
	var cost: float = sim.expansion_cost()
	var affordable: bool = sim.balance >= cost
	var text := "UNBUILT  ·  Expand (X): +%d stalls for $%d" % [grid.next_section_stalls(), int(cost)]
	var font := ThemeDB.fallback_font
	var size := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, 15)
	var center := Vector2(grid.width * TILE * 0.5, mid_y)
	var box := Rect2(center - size * 0.5 - Vector2(12, 8), size + Vector2(24, 14))
	draw_rect(box, Color(0.05, 0.05, 0.07, 0.85))
	draw_rect(box, Color(0.95, 0.78, 0.2, 0.9) if affordable else Color(0.5, 0.5, 0.55, 0.8), false, 2.0)
	draw_string(font, Vector2(box.position.x + 12, center.y + 5), text, HORIZONTAL_ALIGNMENT_LEFT, -1, 15,
		Color(1.0, 0.88, 0.4) if affordable else Color(0.75, 0.75, 0.8))


func _draw_gate(cell: Vector2i, label: String, color: Color) -> void:
	var rect := Rect2(Vector2(cell) * TILE, Vector2(TILE, TILE))
	draw_rect(rect, Color(color, 0.25))
	var barrier := Art.prop("gate")
	if barrier != null:
		_draw_sprite(barrier, rect.get_center(), 0.0, TILE * 1.05, false)
	var font := ThemeDB.fallback_font
	draw_string(font, rect.get_center() + Vector2(-TILE, -TILE * 0.7), label, HORIZONTAL_ALIGNMENT_CENTER, TILE * 2, 13, color.lightened(0.3))


func _draw_incident_art(inc, icon: Texture2D, urgent: bool) -> void:
	var center := cell_center(inc.cell)
	var decal_name: String = INCIDENT_DECALS.get(inc.type_name, "")
	if decal_name != "":
		var decal := Art.prop(decal_name)
		if decal != null:
			var decal_tint := Color(0.4, 0.6, 1.0, 0.8) if inc.type_name == "PipeBurst" else Color(1, 1, 1, 0.9)
			_draw_sprite(decal, center, 0.0, TILE * 0.95, false, decal_tint)
	var color := Color(1.0, 0.55, 0.1)
	match inc.state:
		C.IncidentState.DECISION_PENDING:
			color = Color(1.0, 0.2, 0.2)
		C.IncidentState.IN_PROGRESS:
			color = Color(1.0, 0.85, 0.2)
	var r := TILE * (0.40 + 0.05 * sin(_pulse))
	draw_arc(center, r, 0, TAU, 28, color, 2.5)
	if urgent:
		draw_arc(center, r + 5.0, 0, TAU, 28, Color(1, 1, 1, 0.85), 2.0)
	# Icon floats just above the tile and bobs gently.
	var icon_pos := center + Vector2(0, -TILE * (0.55 + 0.05 * sin(_pulse * 1.3)))
	_draw_sprite(icon, icon_pos, 0.0, TILE * 0.62, true)


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
	if grid.is_locked(hovered_cell):
		return "(%d,%d) Unbuilt — expand the garage (X) to build here" % [hovered_cell.x, hovered_cell.y]
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
