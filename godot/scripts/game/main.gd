extends Node2D
## Playtest entry point. Owns the simulation, drives it at 20Hz × speed,
## and wires input, camera, HUD and the session logger together.
##
## Optional command-line flags (after `--`):
##   --seed=N            fixed seed (default: random)
##   --speed=N           starting speed 1|2|4|8|16
##   --demand=X          starting demand scale (default 1.0)
##   --warmup-ticks=N    run N sim ticks instantly before the first frame
##   --autoplay          pick decision branches automatically (demo / soak)
##   --screenshot=PATH   save a PNG after --screenshot-frames frames, then quit
##   --screenshot-frames=N  (default 30)
##   --spawn-incident=TYPE  spawn one incident at start (e.g. SuspiciousVehicle)
##   --show-summary      open the session summary overlay at start
##   --no-autopause      don't pause when a decision card appears
##   --mode=tutorial     start the tutorial (normally chosen in the menu)
##   --tutorial-skip-to=N  play tutorial steps automatically up to step N

const C = preload("res://scripts/sim/sim_const.gd")
const Simulation = preload("res://scripts/sim/simulation.gd")
const FloorView = preload("res://scripts/game/floor_view.gd")
const Hud = preload("res://scripts/game/hud.gd")
const PlaytestLogger = preload("res://scripts/game/playtest_logger.gd")
const GameConfig = preload("res://scripts/game/game_config.gd")
const Tutorial = preload("res://scripts/game/tutorial.gd")

const MENU_SCENE := "res://scenes/menu.tscn"
const GAME_SCENE := "res://scenes/main.tscn"
const TUTORIAL_SEED := 2024

const SPEEDS := [1, 2, 4, 8, 16]
const MAX_TICKS_PER_FRAME := 400
const PAN_SPEED := 700.0
const DEMAND_MIN := 0.25
const DEMAND_MAX := 8.0

var sim: Simulation
var view: FloorView
var hud: Hud
var camera: Camera2D
var logger: PlaytestLogger

var speed_index := 0
var paused := false
var demand_scale := 1.0
var tick_accum := 0.0

var _opts: Dictionary = {}
var _frame := 0
var _dragging := false
var _end_overlay_shown := false
var auto_pause := true             # pause when a decision card appears (P toggles)
var _paused_for_decision := false
var game_mode := "play"            # "play" or "tutorial"
var tutorial: Tutorial = null


func _ready() -> void:
	_parse_args()
	game_mode = String(_opts.get("mode", GameConfig.mode))
	speed_index = maxi(0, SPEEDS.find(int(_opts.get("speed", "1"))))
	demand_scale = clampf(float(_opts.get("demand", "1.0")), DEMAND_MIN, DEMAND_MAX)

	camera = Camera2D.new()
	add_child(camera)
	camera.make_current()

	view = FloorView.new()
	add_child(view)

	hud = Hud.new()
	add_child(hud)
	hud.branch_chosen.connect(_choose_branch)
	hud.hire_requested.connect(_hire)
	hud.fire_requested.connect(_fire)
	hud.loan_requested.connect(_take_loan)
	hud.speed_step_requested.connect(_step_speed)
	hud.pause_toggled.connect(_toggle_pause)
	hud.menu_requested.connect(go_to_menu)
	hud.tutorial_next_requested.connect(_on_tutorial_next)
	hud.tutorial_exit_requested.connect(go_to_menu)

	_start_session()


func _parse_args() -> void:
	for arg in OS.get_cmdline_user_args():
		var a := String(arg)
		if not a.begins_with("--"):
			continue
		var eq := a.find("=")
		if eq == -1:
			_opts[a.substr(2)] = "true"
		else:
			_opts[a.substr(2, eq - 2)] = a.substr(eq + 1)


func _start_session() -> void:
	var is_tutorial := game_mode == "tutorial"
	var default_seed := TUTORIAL_SEED if is_tutorial else randi() % 1000000
	var session_seed := int(_opts.get("seed", str(default_seed)))
	sim = Simulation.new()
	var ok := sim.setup(session_seed, null, C.TUTORIAL_LOT_SIZE if is_tutorial else C.START_LOT_SIZE)
	sim.demand_scale = demand_scale
	if is_tutorial:
		# A calm, scripted lot: the tutorial spawns its own cars and incidents.
		sim.demand_scale = 0.0
		sim._next_incident_tick.clear()
		sim.city_events_enabled = false
	view.sim = sim
	hud.sim = sim
	sim.day_ended.connect(_on_day_ended)
	sim.decision_required.connect(_on_decision_required)
	auto_pause = not _opts.has("no-autopause")
	_paused_for_decision = false
	paused = false

	if not ok:
		for e in sim.setup_errors:
			sim.log_message("DATA ERROR: %s" % e, "bad")
			push_error(e)

	# Starter crew — same as the UE GameMode.
	sim.hire_staff(C.Role.ATTENDANT)
	sim.hire_staff(C.Role.SECURITY)
	sim.hire_staff(C.Role.MAINTENANCE)
	if is_tutorial:
		sim.log_message("Tutorial: follow the panel at the top of the screen.", "event")
	else:
		sim.log_message("Welcome to SUBLEVEL. Keep the garage solvent for %d days." % C.GOAL_DAYS, "event")

	logger = PlaytestLogger.new()
	logger.start(sim, {"demand_scale": demand_scale, "speed": SPEEDS[speed_index],
		"autoplay": _opts.has("autoplay"), "auto_pause": auto_pause, "mode": game_mode,
		"data_dir": sim.data.data_dir})
	print("[SUBLEVEL] Session seed %d — log: %s" % [session_seed, logger.absolute_path()])

	for _i in int(_opts.get("warmup-ticks", "0")):
		sim.tick()
		if _opts.has("autoplay"):
			_autoplay_step()

	# Debug flags for screenshot coverage of specific UI states.
	if _opts.has("spawn-incident"):
		sim.spawn_incident(String(_opts["spawn-incident"]))

	tick_accum = 0.0
	_end_overlay_shown = false
	hud.set_overlay_visible(_opts.has("show-summary"))

	tutorial = null
	if is_tutorial:
		tutorial = Tutorial.new(self)
		tutorial.start()
		if _opts.has("tutorial-skip-to"):
			tutorial.skip_to(int(_opts["tutorial-skip-to"]))
	hud.tutorial = tutorial

	_update_speed_text()
	_center_camera()


func _restart() -> void:
	logger.write(sim, "restart")
	_opts.erase("seed")          # a restart gets a fresh seed
	_opts.erase("warmup-ticks")
	_opts.erase("spawn-incident")
	_opts.erase("show-summary")
	_opts.erase("tutorial-skip-to")
	_start_session()


func go_to_menu() -> void:
	if logger != null and sim != null:
		logger.write(sim, "menu")
	get_tree().change_scene_to_file(MENU_SCENE)


func _on_tutorial_next() -> void:
	if tutorial == null:
		return
	if tutorial.is_finished():
		# Last step: "Start a real game".
		logger.write(sim, "tutorial_done")
		GameConfig.mode = "play"
		get_tree().change_scene_to_file(GAME_SCENE)
	else:
		tutorial.next()


# ── Frame loop ───────────────────────────────────────────────────

func _process(delta: float) -> void:
	_frame += 1
	var running := not paused and not sim.bankrupt
	if running:
		tick_accum += delta * SPEEDS[speed_index]
		var steps := 0
		while tick_accum >= C.TICK_SECONDS and steps < MAX_TICKS_PER_FRAME:
			sim.tick()
			tick_accum -= C.TICK_SECONDS
			steps += 1
			if _opts.has("autoplay"):
				_autoplay_step()
		if steps >= MAX_TICKS_PER_FRAME:
			tick_accum = 0.0   # machine can't keep up — drop the backlog
		view.tick_frac = clampf(tick_accum / C.TICK_SECONDS, 0.0, 1.0)

	if tutorial != null:
		tutorial.update(delta)
		view.highlight_cells = tutorial.highlights()
	else:
		view.highlight_cells.clear()

	_pan_with_keys(delta)
	hud.hover_text = view.describe_hovered()
	hud.refresh(delta)

	if tutorial == null and (sim.bankrupt or sim.goal_reached) and not _end_overlay_shown:
		_end_overlay_shown = true
		hud.set_overlay_visible(true)
		logger.write(sim, "bankrupt" if sim.bankrupt else "goal_reached")

	if _opts.has("screenshot") and _frame == int(_opts.get("screenshot-frames", "30")):
		_save_screenshot(String(_opts["screenshot"]), true)


func _on_day_ended(summary: Dictionary) -> void:
	logger.write(sim, "day_end")
	print("[SUBLEVEL] Day %d: revenue %.0f, expenses %.0f, balance %.0f, served %d, turned away %d, lost %d" % [
		summary["day"], summary["revenue"], summary["expenses"], summary["balance_end"],
		summary["served"], summary["turned_away"], summary["lost_at_gate"]])


func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_CLOSE_REQUEST and sim != null and logger != null:
		logger.write(sim, "quit")
		print("[SUBLEVEL] Playtest log saved: %s" % logger.absolute_path())


# ── Player actions ───────────────────────────────────────────────

func _choose_branch(index: int) -> void:
	var inc = sim.next_decision()
	if inc == null:
		return
	if sim.resolve_decision(inc.id, index):
		logger.note(sim, "decision", {"incident": inc.type_name, "branch": index})
		if _paused_for_decision and sim.next_decision() == null:
			_paused_for_decision = false
			paused = false
			_update_speed_text()


## Decision timers run on game time, so at ×8 a 45s card lasts under 6 real
## seconds. Auto-pause gives the player time to read it (toggle with P).
func _on_decision_required(_incident) -> void:
	if auto_pause and not _opts.has("autoplay") and not paused:
		paused = true
		_paused_for_decision = true
		_update_speed_text()


func _toggle_auto_pause() -> void:
	auto_pause = not auto_pause
	sim.log_message("Auto-pause on decisions: %s" % ("ON" if auto_pause else "OFF"), "info")
	logger.note(sim, "auto_pause", auto_pause)
	_update_speed_text()


func _hire(role: int) -> void:
	if sim.hire_staff(role) != null:
		logger.note(sim, "hire", C.ROLE_NAMES[role])


func _fire(role: int) -> void:
	if sim.fire_staff(role):
		logger.note(sim, "fire", C.ROLE_NAMES[role])


func _hire_or_fire(role: int, fire: bool) -> void:
	if fire:
		_fire(role)
	else:
		_hire(role)


func _take_loan() -> void:
	if sim.take_loan():
		logger.note(sim, "loan", C.LOAN_AMOUNT)


func _step_speed(step: int) -> void:
	speed_index = clampi(speed_index + step, 0, SPEEDS.size() - 1)
	paused = false
	_update_speed_text()
	logger.note(sim, "speed", SPEEDS[speed_index])


func _toggle_pause() -> void:
	paused = not paused
	_paused_for_decision = false
	_update_speed_text()


func _set_demand(value: float) -> void:
	demand_scale = clampf(value, DEMAND_MIN, DEMAND_MAX)
	sim.demand_scale = demand_scale
	_update_speed_text()
	sim.log_message("Demand scale ×%s" % _fmt_scale(demand_scale), "info")
	logger.note(sim, "demand_scale", demand_scale)


func _fmt_scale(v: float) -> String:
	return str(snappedf(v, 0.01))


func _update_speed_text() -> void:
	if _paused_for_decision:
		hud.speed_text = "PAUSED — decide (1-3)"
	elif paused:
		hud.speed_text = "PAUSED"
	else:
		hud.speed_text = "Speed ×%d" % SPEEDS[speed_index]
	if not auto_pause:
		hud.speed_text += " · auto-pause off"
	hud.demand_scale_text = _fmt_scale(demand_scale)


## --autoplay: decide each pending card after a short random delay.
func _autoplay_step() -> void:
	var inc = sim.next_decision()
	if inc == null or sim.rng.randf() > 0.02:
		return
	var options: Array[int] = []
	for i in inc.def.get("branches", []).size():
		if sim.is_branch_available(inc, i):
			options.append(i)
	if not options.is_empty():
		# Draws from the sim's RNG, so an autoplayed run is reproducible from its seed.
		sim.resolve_decision(inc.id, options[sim.rng.randi_range(0, options.size() - 1)])


# ── Input ────────────────────────────────────────────────────────

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		_handle_key(event as InputEventKey)
	elif event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_WHEEL_UP and mb.pressed:
			_zoom(1.1)
		elif mb.button_index == MOUSE_BUTTON_WHEEL_DOWN and mb.pressed:
			_zoom(1.0 / 1.1)
		elif mb.button_index == MOUSE_BUTTON_MIDDLE or mb.button_index == MOUSE_BUTTON_RIGHT:
			_dragging = mb.pressed
	elif event is InputEventMouseMotion and _dragging:
		camera.position -= (event as InputEventMouseMotion).relative / camera.zoom


func _handle_key(k: InputEventKey) -> void:
	match k.keycode:
		KEY_SPACE:
			_toggle_pause()
		KEY_P:
			_toggle_auto_pause()
		KEY_EQUAL, KEY_PLUS, KEY_KP_ADD:
			_step_speed(1)
		KEY_MINUS, KEY_KP_SUBTRACT:
			_step_speed(-1)
		KEY_1, KEY_KP_1:
			_choose_branch(0)
		KEY_2, KEY_KP_2:
			_choose_branch(1)
		KEY_3, KEY_KP_3:
			_choose_branch(2)
		KEY_F1:
			_hire_or_fire(C.Role.ATTENDANT, k.shift_pressed)
		KEY_F2:
			_hire_or_fire(C.Role.SECURITY, k.shift_pressed)
		KEY_F3:
			_hire_or_fire(C.Role.MAINTENANCE, k.shift_pressed)
		KEY_L:
			_take_loan()
		KEY_BRACKETLEFT:
			_set_demand(demand_scale * 0.5)
		KEY_BRACKETRIGHT:
			_set_demand(demand_scale * 2.0)
		KEY_TAB:
			hud.toggle_overlay()
		KEY_ESCAPE:
			hud.set_overlay_visible(false)
		KEY_R:
			if hud.overlay_visible:
				_restart()
		KEY_M:
			go_to_menu()
		KEY_ENTER, KEY_KP_ENTER:
			_on_tutorial_next()
		KEY_F12:
			DirAccess.make_dir_recursive_absolute("user://screenshots")
			var stamp := Time.get_datetime_string_from_system(false, true).replace(":", "-").replace(" ", "_")
			_save_screenshot("user://screenshots/sublevel_%s.png" % stamp, false)


# ── Camera ───────────────────────────────────────────────────────

func _center_camera() -> void:
	# Fit the lot into the space left by the HUD: side panels, top bar (and the
	# tutorial panel), and the decision card at the bottom.
	var vp := get_viewport().get_visible_rect().size
	var top := 60.0 + (190.0 if tutorial != null else 0.0)
	var bottom := 230.0
	var left := Hud.LEFT_W + 30.0
	var right := Hud.RIGHT_W + 30.0
	var free := Vector2(vp.x - left - right, vp.y - top - bottom)
	var lot := view.grid_pixel_size()
	var z := clampf(minf(free.x / lot.x, free.y / lot.y), 0.5, 1.6)
	camera.zoom = Vector2(z, z)
	var free_center := Vector2(left + free.x * 0.5, top + free.y * 0.5)
	camera.position = lot * 0.5 + (vp * 0.5 - free_center) / z


func _zoom(factor: float) -> void:
	var z := clampf(camera.zoom.x * factor, 0.4, 3.0)
	camera.zoom = Vector2(z, z)


func _pan_with_keys(delta: float) -> void:
	var dir := Vector2.ZERO
	if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP):
		dir.y -= 1.0
	if Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN):
		dir.y += 1.0
	if Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT):
		dir.x -= 1.0
	if Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT):
		dir.x += 1.0
	if dir != Vector2.ZERO:
		camera.position += dir.normalized() * PAN_SPEED * delta / camera.zoom.x


func _save_screenshot(path: String, quit_after: bool) -> void:
	await RenderingServer.frame_post_draw
	var img := get_viewport().get_texture().get_image()
	if img == null:
		# --headless uses a dummy renderer with no pixels; run under a display (or xvfb).
		push_error("Screenshot unavailable: no rendered image (running --headless?)")
		if quit_after:
			get_tree().quit(1)
		return
	var err := img.save_png(path)
	if err == OK:
		print("[SUBLEVEL] Screenshot saved: %s" % ProjectSettings.globalize_path(path))
		sim.log_message("Screenshot saved.", "info")
	else:
		push_error("Screenshot failed (%s): %s" % [error_string(err), path])
	if quit_after:
		logger.write(sim, "screenshot")
		get_tree().quit()
