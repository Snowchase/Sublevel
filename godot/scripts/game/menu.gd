extends Control
## Start menu: New Game, Tutorial, How to Play, Quit — over an animated
## garage backdrop. Built in code like the HUD. The chosen mode is handed
## to the game scene through game_config.gd.
##
## Optional flags (after `--`): --screenshot=PATH [--screenshot-frames=N]
## --show-help (open the How to Play panel, for screenshots)

const GameConfig = preload("res://scripts/game/game_config.gd")
const DataLoader = preload("res://scripts/sim/data_loader.gd")

const GAME_SCENE := "res://scenes/main.tscn"
const GOLD := Color(0.95, 0.82, 0.30)

var _buttons: Dictionary = {}          # name -> Button
var _help_panel: PanelContainer
var _status_label: Label
var _cars: Array = []                  # backdrop traffic: {x, lane, speed, color}
var _rng := RandomNumberGenerator.new()
var _opts: Dictionary = {}
var _frame := 0


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	for arg in OS.get_cmdline_user_args():
		var a := String(arg)
		if a.begins_with("--"):
			var eq := a.find("=")
			_opts[a.substr(2) if eq == -1 else a.substr(2, eq - 2)] = "true" if eq == -1 else a.substr(eq + 1)

	_rng.seed = 7
	for i in 14:
		_cars.append(_new_car(_rng.randf_range(0.0, 1.0)))

	_build()
	_check_data()
	if _opts.has("show-help"):
		_help_panel.visible = true


# ── Construction ─────────────────────────────────────────────────

func _build() -> void:
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)

	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 10)
	column.alignment = BoxContainer.ALIGNMENT_CENTER
	center.add_child(column)

	var title := Label.new()
	title.text = "SUBLEVEL"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 76)
	title.add_theme_color_override("font_color", GOLD)
	title.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.8))
	title.add_theme_constant_override("shadow_offset_x", 3)
	title.add_theme_constant_override("shadow_offset_y", 3)
	column.add_child(title)

	var subtitle := Label.new()
	subtitle.text = "Run the garage. Survive the city."
	subtitle.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	subtitle.add_theme_font_size_override("font_size", 18)
	subtitle.add_theme_color_override("font_color", Color(0.75, 0.77, 0.82))
	column.add_child(subtitle)

	var gap := Control.new()
	gap.custom_minimum_size = Vector2(0, 24)
	column.add_child(gap)

	var tutorial_done := GameConfig.is_tutorial_done()
	_add_button(column, "new_game", "New Game", func(): start_game("play"))
	_add_button(column, "tutorial", "Tutorial  ✓" if tutorial_done else "Tutorial  — start here",
		func(): start_game("tutorial"))
	_add_button(column, "help", "How to Play", func(): _help_panel.visible = not _help_panel.visible)
	_add_button(column, "quit", "Quit", func(): get_tree().quit())

	if not tutorial_done:
		_buttons["tutorial"].add_theme_color_override("font_color", GOLD)

	_status_label = Label.new()
	_status_label.add_theme_font_size_override("font_size", 12)
	_status_label.add_theme_color_override("font_color", Color(0.5, 0.52, 0.58))
	_status_label.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	_status_label.offset_top = -28
	_status_label.offset_left = 12
	add_child(_status_label)

	_build_help()

	# Keyboard: focus the most sensible first choice.
	(_buttons["new_game"] if tutorial_done else _buttons["tutorial"]).grab_focus()


func _add_button(parent: Control, key: String, text: String, callback: Callable) -> void:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = Vector2(320, 50)
	b.add_theme_font_size_override("font_size", 20)
	b.pressed.connect(callback)
	parent.add_child(b)
	_buttons[key] = b


func _build_help() -> void:
	_help_panel = PanelContainer.new()
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.07, 0.08, 0.10, 0.97)
	sb.border_color = Color(0.3, 0.32, 0.38)
	sb.set_border_width_all(1)
	sb.set_corner_radius_all(6)
	sb.set_content_margin_all(18)
	_help_panel.add_theme_stylebox_override("panel", sb)
	_help_panel.set_anchors_preset(Control.PRESET_CENTER)
	_help_panel.custom_minimum_size = Vector2(620, 0)
	_help_panel.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_help_panel.grow_vertical = Control.GROW_DIRECTION_BOTH
	add_child(_help_panel)

	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 10)
	_help_panel.add_child(box)

	var text := RichTextLabel.new()
	text.bbcode_enabled = true
	text.fit_content = true
	text.scroll_active = false
	text.add_theme_font_size_override("normal_font_size", 14)
	text.add_theme_font_size_override("bold_font_size", 14)
	text.text = HELP_TEXT
	box.add_child(text)

	var close := Button.new()
	close.text = "Close"
	close.custom_minimum_size = Vector2(120, 36)
	close.size_flags_horizontal = Control.SIZE_SHRINK_END
	close.pressed.connect(func(): _help_panel.visible = false)
	box.add_child(close)
	_help_panel.visible = false


# gdlint: disable=max-line-length
const HELP_TEXT := """[font_size=22][color=#f2d24a]How to play[/color][/font_size]

You run a small underground garage. Cars enter at the green [b]IN[/b] gate, park, and pay when they leave by the red [b]OUT[/b] gate. Every midnight you pay [b]rent and wages[/b]. Keep the balance above [b]−$2,000[/b] for [b]7 days[/b].

[b]Reputation[/b] decides how many customers come. Serve cars well and it rises; turn cars away or ignore problems and it falls.

[b]Staff[/b]: Attendants run the gate, Security deters crime, Maintenance cleans up incidents.

[table=2][cell][b]Space[/b]  [/cell][cell]Pause / resume[/cell][cell][b]+ / −[/b]  [/cell][cell]Change speed (×1 to ×16)[/cell][cell][b]1 2 3[/b]  [/cell][cell]Answer a decision card (or click it)[/cell][cell][b]F1 F2 F3[/b]  [/cell][cell]Hire Attendant / Security / Maintenance (Shift = let go)[/cell][cell][b]L[/b]  [/cell][cell]Take a $5,000 loan[/cell][cell][b]WASD[/b], wheel  [/cell][cell]Pan, zoom[/cell][cell][b]Tab[/b]  [/cell][cell]Day-by-day summary[/cell][cell][b]P[/b]  [/cell][cell]Auto-pause on decisions[/cell][cell][b]M[/b]  [/cell][cell]Back to this menu[/cell][/table]

First time? Play the [b]Tutorial[/b] — it takes about 5 minutes."""
# gdlint: enable=max-line-length


func _check_data() -> void:
	var loader := DataLoader.new()
	if loader.load_all():
		_status_label.text = "Playtest build · Godot %s · data: %s" % [Engine.get_version_info()["string"], loader.data_dir]
	else:
		_status_label.text = "DATA ERROR: %s — keep the godot folder next to the repo's Data folder." % "; ".join(PackedStringArray(loader.errors))
		_status_label.add_theme_color_override("font_color", Color(1.0, 0.45, 0.4))


# ── Actions ──────────────────────────────────────────────────────

## Sets the mode for the game scene and returns the scene to load.
## Split from start_game() so tests can check it without changing scenes.
func choose_mode(game_mode: String) -> String:
	GameConfig.mode = game_mode
	return GAME_SCENE


func start_game(game_mode: String) -> void:
	get_tree().change_scene_to_file(choose_mode(game_mode))


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		match (event as InputEventKey).keycode:
			KEY_ESCAPE:
				_help_panel.visible = false
			KEY_T:
				start_game("tutorial")


# ── Animated backdrop ────────────────────────────────────────────

const CAR_COLORS := [Color(0.30, 0.80, 0.80), Color(0.78, 0.78, 0.84), Color(0.90, 0.58, 0.20),
	Color(0.70, 0.40, 0.90), Color(0.30, 0.55, 1.00)]
const LANES := [0.18, 0.42, 0.66, 0.90]   # lane rows as a fraction of screen height


func _new_car(x_frac: float) -> Dictionary:
	var lane := _rng.randi_range(0, LANES.size() - 1)
	return {
		"x": x_frac, "lane": lane,
		"speed": _rng.randf_range(0.03, 0.07) * (1.0 if lane % 2 == 0 else -1.0),
		"color": CAR_COLORS[_rng.randi_range(0, CAR_COLORS.size() - 1)],
	}


func _process(delta: float) -> void:
	_frame += 1
	for car in _cars:
		car["x"] = float(car["x"]) + float(car["speed"]) * delta
		if car["x"] > 1.1:
			car["x"] = -0.1
		elif car["x"] < -0.1:
			car["x"] = 1.1
	queue_redraw()
	if _opts.has("screenshot") and _frame == int(_opts.get("screenshot-frames", "30")):
		_save_screenshot(String(_opts["screenshot"]))


func _draw() -> void:
	var s := size
	draw_rect(Rect2(Vector2.ZERO, s), Color(0.05, 0.05, 0.07))
	var stall_w := 34.0
	var stall_h := s.y * 0.09
	# Stall rows between the lanes, drawn faintly.
	for li in LANES.size():
		var lane_y: float = float(LANES[li]) * s.y
		draw_rect(Rect2(0, lane_y - 14, s.x, 28), Color(0.09, 0.09, 0.11))
		for row_side: int in [-1, 1]:
			var y: float = lane_y + row_side * (14 + stall_h * 0.5) - stall_h * 0.5
			var x := 0.0
			while x < s.x:
				draw_rect(Rect2(x + 2, y, stall_w - 4, stall_h), Color(0.12, 0.15, 0.20, 0.55))
				x += stall_w
	for car in _cars:
		var lane_y: float = float(LANES[int(car["lane"])]) * s.y
		var pos := Vector2(float(car["x"]) * s.x, lane_y)
		draw_rect(Rect2(pos - Vector2(13, 7), Vector2(26, 14)), Color(car["color"], 0.45))
	# Darken behind the menu column so text stays readable.
	draw_rect(Rect2(Vector2(s.x * 0.5 - 230, 0), Vector2(460, s.y)), Color(0.03, 0.03, 0.05, 0.55))


func _save_screenshot(path: String) -> void:
	await RenderingServer.frame_post_draw
	var img := get_viewport().get_texture().get_image()
	if img == null:
		push_error("Screenshot unavailable: no rendered image (running --headless?)")
		get_tree().quit(1)
		return
	if img.save_png(path) == OK:
		print("[SUBLEVEL] Screenshot saved: %s" % ProjectSettings.globalize_path(path))
	get_tree().quit()
