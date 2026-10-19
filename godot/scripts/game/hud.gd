extends CanvasLayer
## Playtest HUD, built entirely in code (no .tscn layout to break).
##   top bar     — day, clock, speed, money, goal
##   left panel  — factions, city, lot, staff + hire/fire/loan/speed buttons
##   right panel — open incidents + event log
##   bottom      — decision card with clickable branches, hover info, help
##   overlay     — session summary (Tab), bankrupt / goal screens

const C = preload("res://scripts/sim/sim_const.gd")
const Simulation = preload("res://scripts/sim/simulation.gd")

signal branch_chosen(index: int)
signal hire_requested(role: int)
signal fire_requested(role: int)
signal loan_requested
signal speed_step_requested(step: int)
signal pause_toggled

const LEFT_W := 268.0
const RIGHT_W := 300.0
const MARGIN := 8.0

const KIND_COLORS := {
	"info": "#b8bcc6", "good": "#7be07b", "bad": "#ff6b5e",
	"warn": "#ffb347", "event": "#6fd3ff", "decision": "#ff8080",
}

var sim: Simulation = null
var hover_text := ""
var speed_text := ""
var demand_scale_text := ""

var _top_label: Label
var _left_info: RichTextLabel
var _incident_text: RichTextLabel
var _log_text: RichTextLabel
var _decision_panel: PanelContainer
var _decision_title: Label
var _decision_info: Label
var _branch_buttons: Array[Button] = []
var _hover_label: Label
var _help_label: Label
var _overlay: PanelContainer
var _overlay_text: RichTextLabel
var _left_panel: PanelContainer
var _right_panel: PanelContainer
var _top_panel: PanelContainer

var _slow_refresh := 0.0
var overlay_visible := false


func _ready() -> void:
	_build()
	get_viewport().size_changed.connect(_layout)
	_layout()


# ── Construction ─────────────────────────────────────────────────

func _panel_style(alpha: float = 0.86) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.07, 0.08, 0.10, alpha)
	sb.border_color = Color(0.25, 0.27, 0.32)
	sb.set_border_width_all(1)
	sb.set_corner_radius_all(4)
	sb.content_margin_left = 10
	sb.content_margin_right = 10
	sb.content_margin_top = 8
	sb.content_margin_bottom = 8
	return sb


func _make_panel(alpha: float = 0.86) -> PanelContainer:
	var p := PanelContainer.new()
	p.add_theme_stylebox_override("panel", _panel_style(alpha))
	add_child(p)
	return p


func _make_rich(font_size: int = 13) -> RichTextLabel:
	var r := RichTextLabel.new()
	r.bbcode_enabled = true
	r.scroll_active = false
	r.fit_content = true
	r.add_theme_font_size_override("normal_font_size", font_size)
	r.add_theme_font_size_override("bold_font_size", font_size)
	r.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return r


func _make_button(text: String, callback: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.focus_mode = Control.FOCUS_NONE
	b.add_theme_font_size_override("font_size", 12)
	b.pressed.connect(callback)
	return b


func _build() -> void:
	# Top bar
	_top_panel = _make_panel()
	_top_label = Label.new()
	_top_label.add_theme_font_size_override("font_size", 15)
	_top_panel.add_child(_top_label)

	# Left panel
	_left_panel = _make_panel()
	var left_box := VBoxContainer.new()
	left_box.add_theme_constant_override("separation", 6)
	_left_panel.add_child(left_box)
	_left_info = _make_rich(13)
	left_box.add_child(_left_info)

	var hire_row := HBoxContainer.new()
	hire_row.add_child(_make_button("+Attendant", func(): hire_requested.emit(C.Role.ATTENDANT)))
	hire_row.add_child(_make_button("+Security", func(): hire_requested.emit(C.Role.SECURITY)))
	hire_row.add_child(_make_button("+Maint.", func(): hire_requested.emit(C.Role.MAINTENANCE)))
	left_box.add_child(hire_row)

	var fire_row := HBoxContainer.new()
	fire_row.add_child(_make_button("−Attendant", func(): fire_requested.emit(C.Role.ATTENDANT)))
	fire_row.add_child(_make_button("−Security", func(): fire_requested.emit(C.Role.SECURITY)))
	fire_row.add_child(_make_button("−Maint.", func(): fire_requested.emit(C.Role.MAINTENANCE)))
	left_box.add_child(fire_row)

	var misc_row := HBoxContainer.new()
	misc_row.add_child(_make_button("Loan $%d" % int(C.LOAN_AMOUNT), func(): loan_requested.emit()))
	misc_row.add_child(_make_button("Pause", func(): pause_toggled.emit()))
	misc_row.add_child(_make_button("Slower", func(): speed_step_requested.emit(-1)))
	misc_row.add_child(_make_button("Faster", func(): speed_step_requested.emit(1)))
	left_box.add_child(misc_row)

	# Right panel
	_right_panel = _make_panel()
	var right_box := VBoxContainer.new()
	right_box.add_theme_constant_override("separation", 8)
	_right_panel.add_child(right_box)
	_incident_text = _make_rich(12)
	right_box.add_child(_incident_text)
	var sep := HSeparator.new()
	right_box.add_child(sep)
	_log_text = _make_rich(12)
	right_box.add_child(_log_text)

	# Decision card
	_decision_panel = _make_panel(0.95)
	var card := VBoxContainer.new()
	card.add_theme_constant_override("separation", 6)
	_decision_panel.add_child(card)
	_decision_title = Label.new()
	_decision_title.add_theme_font_size_override("font_size", 18)
	_decision_title.add_theme_color_override("font_color", Color(1, 0.45, 0.4))
	card.add_child(_decision_title)
	_decision_info = Label.new()
	_decision_info.add_theme_font_size_override("font_size", 12)
	_decision_info.add_theme_color_override("font_color", Color(0.75, 0.77, 0.82))
	card.add_child(_decision_info)
	for i in 3:
		var idx := i
		var b := _make_button("", func(): branch_chosen.emit(idx))
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.add_theme_font_size_override("font_size", 14)
		card.add_child(b)
		_branch_buttons.append(b)
	_decision_panel.visible = false

	# Hover + help lines
	_hover_label = Label.new()
	_hover_label.add_theme_font_size_override("font_size", 12)
	_hover_label.add_theme_color_override("font_color", Color(0.85, 0.85, 0.6))
	add_child(_hover_label)
	_help_label = Label.new()
	_help_label.add_theme_font_size_override("font_size", 11)
	_help_label.add_theme_color_override("font_color", Color(0.55, 0.57, 0.62))
	_help_label.text = "Space pause · P auto-pause · +/- speed · 1-3 decide · F1-F3 hire (Shift = fire) · L loan" \
		+ " · [ ] demand · WASD/drag pan · wheel zoom · Tab summary · F12 screenshot"
	add_child(_help_label)

	# Summary overlay
	_overlay = _make_panel(0.96)
	_overlay_text = _make_rich(13)
	_overlay_text.fit_content = false
	_overlay_text.scroll_active = true
	_overlay_text.mouse_filter = Control.MOUSE_FILTER_STOP
	_overlay.add_child(_overlay_text)
	_overlay.visible = false


func _layout() -> void:
	var vp := get_viewport().get_visible_rect().size
	_top_panel.position = Vector2(MARGIN, MARGIN)
	_top_panel.size = Vector2(vp.x - MARGIN * 2, 34)

	_left_panel.position = Vector2(MARGIN, 50)
	_left_panel.size = Vector2(LEFT_W, 0)
	_left_panel.custom_minimum_size = Vector2(LEFT_W, 0)

	_right_panel.position = Vector2(vp.x - RIGHT_W - MARGIN, 50)
	_right_panel.size = Vector2(RIGHT_W, 0)
	_right_panel.custom_minimum_size = Vector2(RIGHT_W, 0)

	var card_w := 520.0
	_decision_panel.custom_minimum_size = Vector2(card_w, 0)
	_decision_panel.position = Vector2((vp.x - card_w) * 0.5, vp.y - 210)

	_hover_label.position = Vector2(LEFT_W + MARGIN * 3, vp.y - 42)
	_help_label.position = Vector2(MARGIN + 4, vp.y - 20)

	_overlay.position = Vector2(vp.x * 0.12, vp.y * 0.1)
	_overlay.size = Vector2(vp.x * 0.76, vp.y * 0.8)
	_overlay.custom_minimum_size = _overlay.size


# ── Refresh ──────────────────────────────────────────────────────

func refresh(delta: float) -> void:
	if sim == null:
		return
	_refresh_top()
	_refresh_decision()
	_hover_label.text = hover_text

	_slow_refresh -= delta
	if _slow_refresh <= 0.0:
		_slow_refresh = 0.15
		_refresh_left()
		_refresh_incidents()
		_refresh_log()
		if overlay_visible:
			_refresh_overlay()


func _money(amount: float) -> String:
	var sign_text := "-" if amount < 0.0 else ""
	return "%s$%s" % [sign_text, _thousands(int(round(absf(amount))))]


func _thousands(n: int) -> String:
	var s := str(n)
	var out := ""
	while s.length() > 3:
		out = "," + s.substr(s.length() - 3) + out
		s = s.substr(0, s.length() - 3)
	return s + out


func _refresh_top() -> void:
	var day_kind := "weekend" if sim.is_weekend() else "weekday"
	var goal := "GOAL REACHED" if sim.goal_reached else "goal: day %d/%d" % [mini(sim.day, C.GOAL_DAYS), C.GOAL_DAYS]
	if sim.bankrupt:
		goal = "BANKRUPT"
	_top_label.text = "SUBLEVEL playtest   Day %d (%s)  %s   %s   Balance %s   Today +%s / -%s   Loan %s   %s" % [
		sim.day, day_kind, sim.clock_text(), speed_text,
		_money(sim.balance), _money(sim.stats["revenue"]), _money(sim.stats["expenses"]),
		_money(sim.loan_principal), goal]
	_top_label.add_theme_color_override("font_color", Color(1, 0.5, 0.45) if sim.balance < 0.0 else Color(0.92, 0.92, 0.95))


func _bar(value: float) -> String:
	# value in [-100, 100] → 10-cell bar around a center mark
	var cells := 10
	var filled := int(round(absf(value) / 100.0 * cells * 0.5))
	var left := ""
	var right := ""
	for i in cells / 2:
		left += "█" if value < 0.0 and (cells / 2 - i) <= filled else "·"
		right += "█" if value > 0.0 and i < filled else "·"
	var color := "#7be07b" if value > 0.0 else ("#ff6b5e" if value < 0.0 else "#888888")
	return "[color=%s]%s|%s[/color]" % [color, left, right]


func _refresh_left() -> void:
	var t := "[b]FACTIONS[/b]\n"
	for f in C.FACTIONS:
		var v := float(sim.factions[f])
		t += "%-7s %s %+d\n" % [C.FACTION_LABELS[f], _bar(v), int(round(v))]

	t += "\n[b]CITY[/b]\n"
	t += "Demand %.1f cars/min  (×%s)\n" % [sim.current_lambda, demand_scale_text]
	if sim.active_events.is_empty():
		t += "[color=#888888]No city events[/color]\n"
	for e in sim.active_events:
		var left_s := int((int(e["end"]) - sim.tick_count) * C.TICK_SECONDS)
		t += "[color=#6fd3ff]%s[/color] %ds%s\n" % [e["def"]["type"], left_s, "  SURGE" if e["def"]["surge_pricing"] else ""]

	var occupied: int = sim.sample_occupancy()
	var total: int = sim.grid.stalls.size()
	t += "\n[b]LOT[/b]\n"
	t += "Stalls %d / %d   in lot %d   street queue %d\n" % [occupied, total, sim.vehicles.size(), sim.outside_queue.size()]
	t += "Today: served %d · turned away %d · lost at gate %d\n" % [
		sim.stats["served"], sim.stats["turned_away"], sim.stats["lost_at_gate"]]
	var integ_color := "#7be07b" if sim.grid.integrity > 0.6 else ("#ffb347" if sim.grid.integrity > 0.5 else "#ff6b5e")
	t += "Structure [color=%s]%d%%[/color]   cracks %d\n" % [integ_color, int(sim.grid.integrity * 100), sim.grid.active_cracks]

	t += "\n[b]STAFF[/b]  wages %s/day\n" % _money(_daily_wages())
	t += "Attendant %d · Security %d · Maint. %d\n" % [
		sim.count_staff(C.Role.ATTENDANT), sim.count_staff(C.Role.SECURITY), sim.count_staff(C.Role.MAINTENANCE)]
	var tired := 0
	for s in sim.staff.values():
		if s.is_fatigued():
			tired += 1
	if tired > 0:
		t += "[color=#ffb347]%d exhausted[/color]\n" % tired
	_left_info.text = t


func _daily_wages() -> float:
	var w := 0.0
	for s in sim.staff.values():
		w += float(C.STAFF_WAGES[s.role])
	return w


func _refresh_incidents() -> void:
	var t := "[b]INCIDENTS[/b]\n"
	var lines: Array[String] = []
	for inc in sim.incidents.values():
		if not inc.is_open():
			continue
		match inc.state:
			C.IncidentState.DECISION_PENDING:
				lines.push_front("[color=#ff6b5e]DECIDE %ds[/color] %s" % [int(inc.seconds_left(sim.tick_count)), inc.type_name])
			C.IncidentState.IN_PROGRESS:
				var who = sim.staff.get(inc.staff_id)
				lines.append("[color=#ffd84a]handling[/color] %s — %s" % [inc.type_name, who.display_name if who != null else "?"])
			_:
				var role_name: String = C.ROLE_NAMES[int(inc.def.get("auto_resolve_role", C.Role.MAINTENANCE))]
				var until_fine := int((C.AUTO_INCIDENT_TIMEOUT - (sim.tick_count - inc.spawn_tick)) * C.TICK_SECONDS)
				var hint := ""
				if sim.count_staff(int(inc.def.get("auto_resolve_role", C.Role.MAINTENANCE))) == 0:
					hint = " [color=#ff6b5e](no %s on staff!)[/color]" % role_name
				lines.append("[color=#ffb347]waiting[/color] %s — fine in %ds%s" % [inc.type_name, maxi(until_fine, 0), hint])
	if lines.is_empty():
		t += "[color=#888888]All quiet.[/color]"
	else:
		t += "\n".join(PackedStringArray(lines.slice(0, 9)))
		if lines.size() > 9:
			t += "\n[color=#888888]+%d more[/color]" % (lines.size() - 9)
	_incident_text.text = t


func _refresh_log() -> void:
	var t := "[b]LOG[/b]\n"
	var msgs: Array = sim.messages
	var start := maxi(0, msgs.size() - 14)
	for i in range(msgs.size() - 1, start - 1, -1):
		var m: Dictionary = msgs[i]
		var color: String = KIND_COLORS.get(String(m["kind"]), "#b8bcc6")
		t += "[color=#666a73]%s[/color] [color=%s]%s[/color]\n" % [C.tick_to_clock(int(m["tick"])), color, m["text"]]
	_log_text.text = t


func _refresh_decision() -> void:
	var inc = sim.next_decision()
	if inc == null:
		_decision_panel.visible = false
		return
	_decision_panel.visible = true
	var more: int = sim.pending_decision_count() - 1
	_decision_title.text = "DECISION — %s   %ds left" % [inc.type_name, int(ceil(inc.seconds_left(sim.tick_count)))]
	var info := "Severity %d%%" % int(inc.severity * 100)
	if more > 0:
		info += "   ·   %d more decision(s) waiting" % more
	var branches: Array = inc.def.get("branches", [])
	var fallback: int = sim.expiry_branch(inc)   # what actually fires on timeout
	if fallback >= 0:
		info += "   ·   if ignored: %s" % branches[fallback]["label"]
	_decision_info.text = info

	# Rebuilt every frame: availability changes when Security is hired or fired.
	for i in _branch_buttons.size():
		var b := _branch_buttons[i]
		if i >= branches.size():
			b.visible = false
			continue
		b.visible = true
		var br: Dictionary = branches[i]
		var effects := _effect_text(br)
		var available: bool = sim.is_branch_available(inc, i)
		b.disabled = not available
		b.text = "[%d] %s   %s%s" % [i + 1, br["label"], effects, "" if available else "   (needs Security on staff)"]


func _effect_text(branch: Dictionary) -> String:
	var parts: Array[String] = []
	var delta: Dictionary = branch["delta"]
	for f in C.FACTIONS:
		if delta.has(f):
			parts.append("%s %+d" % [C.FACTION_LABELS[f], int(delta[f])])
	var money := float(C.CONSEQUENCE_MONEY.get(String(branch["consequence"]), 0.0))
	if money != 0.0:
		parts.append(("+" if money > 0.0 else "") + _money(money))
	if String(branch["consequence"]) in C.TOW_CONSEQUENCES:
		parts.append("car towed")
	if parts.is_empty():
		return ""
	return "(" + ", ".join(PackedStringArray(parts)) + ")"


# ── Overlay ──────────────────────────────────────────────────────

func toggle_overlay() -> void:
	set_overlay_visible(not overlay_visible)


func set_overlay_visible(value: bool) -> void:
	overlay_visible = value
	_overlay.visible = value
	if value:
		_refresh_overlay()


func _refresh_overlay() -> void:
	var t := ""
	if sim.bankrupt:
		t += "[font_size=26][color=#ff6b5e]BANKRUPT[/color][/font_size]\nYour balance fell below %s. Press [b]R[/b] to restart.\n\n" % _money(C.BANKRUPT_THRESHOLD)
	elif sim.goal_reached:
		t += "[font_size=26][color=#7be07b]GOAL REACHED[/color][/font_size]\nSUBLEVEL survived %d days. Keep playing, or press [b]R[/b] to restart.\n\n" % C.GOAL_DAYS
	else:
		t += "[font_size=22]Session summary[/font_size]   (Tab to close, R to restart)\n\n"

	t += "Seed %d · demand ×%s · day %d %s\n\n" % [sim.seed_value, demand_scale_text, sim.day, sim.clock_text()]
	t += "[table=9]"
	for h in ["Day", "Revenue", "Expenses", "Balance", "Served", "Turned away", "Lost at gate", "Incidents", "Decisions"]:
		t += "[cell][b]%s  [/b][/cell]" % h
	var rows: Array = sim.day_history.duplicate()
	var today: Dictionary = sim.stats.duplicate()
	today["day"] = "%d*" % sim.day
	today["balance_end"] = sim.balance
	rows.append(today)
	for r in rows:
		t += "[cell]%s[/cell][cell]%s[/cell][cell]%s[/cell][cell]%s[/cell][cell]%d[/cell][cell]%d[/cell][cell]%d[/cell][cell]%d[/cell][cell]%d / %d[/cell]" % [
			str(r["day"]), _money(r["revenue"]), _money(r["expenses"]), _money(r["balance_end"]),
			r["served"], r["turned_away"], r["lost_at_gate"], r["incidents"],
			r["decisions_made"], r["decisions_made"] + r["decisions_expired"]]
	t += "[/table]\n[color=#888888]* = day in progress[/color]\n\n"

	t += "[b]Decisions[/b]\n"
	if sim.decision_log.is_empty():
		t += "[color=#888888]None yet.[/color]\n"
	for i in range(sim.decision_log.size() - 1, maxi(-1, sim.decision_log.size() - 13), -1):
		var d: Dictionary = sim.decision_log[i]
		t += "Day %d %s  %s → %s%s\n" % [d["day"], d["clock"], d["incident"], d["branch"],
			"  [color=#ff6b5e](ignored)[/color]" if d["expired"] else ""]
	_overlay_text.text = t
