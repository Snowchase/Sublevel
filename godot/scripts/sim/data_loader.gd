extends RefCounted
## Loads the shared design data (Data/*.json) used by both the UE build
## and this playtest. Search order:
##   1. res://data/            (a copy bundled inside an exported build)
##   2. <project>/../Data/     (the repo layout: godot/ next to Data/)
##   3. <executable dir>/Data/ (an exported build with Data/ beside it)

const C = preload("res://scripts/sim/sim_const.gd")

var data_dir := ""
var errors: Array[String] = []

var incident_defs: Dictionary = {}     # type name -> Dictionary (see _parse_incident)
var vehicle_defs: Dictionary = {}      # type name -> Dictionary
var city_event_defs: Array = []        # Array[Dictionary]
var weekday_curve: Array = []          # 24 floats, vehicles per real minute
var weekend_curve: Array = []


func load_all() -> bool:
	data_dir = _find_data_dir()
	if data_dir == "":
		errors.append("Data folder not found (looked in res://data, ../Data, and next to the executable)")
		return false

	_load_incidents()
	_load_vehicles()
	_load_city_events()
	_load_arrivals()
	return errors.is_empty()


func _find_data_dir() -> String:
	var candidates: Array[String] = [
		"res://data",
		ProjectSettings.globalize_path("res://").path_join("../Data").simplify_path(),
		OS.get_executable_path().get_base_dir().path_join("Data"),
	]
	for dir in candidates:
		if FileAccess.file_exists(dir.path_join("IncidentTypes.json")):
			return dir
	return ""


func _read_json(file_name: String) -> Variant:
	var path := data_dir.path_join(file_name)
	var text := FileAccess.get_file_as_string(path)
	if text == "":
		errors.append("Could not read %s" % path)
		return null
	var parsed: Variant = JSON.parse_string(text)
	if parsed == null:
		errors.append("JSON parse failed: %s" % path)
	return parsed


# ── IncidentTypes.json ───────────────────────────────────────────

func _load_incidents() -> void:
	var root: Variant = _read_json("IncidentTypes.json")
	if not (root is Array):
		return
	for entry in root:
		if not (entry is Dictionary) or not entry.has("type"):
			continue
		var def := _parse_incident(entry)
		incident_defs[def["type"]] = def


func _parse_incident(entry: Dictionary) -> Dictionary:
	var branches: Array = []
	for b in entry.get("branches", []):
		branches.append({
			"label": String(b.get("label", "?")),
			"delta": _parse_delta(b.get("faction_delta", {})),
			"consequence": String(b.get("consequence", "")),
		})
	var default_branch := int(entry.get("default_branch", 0))
	if branches.size() > 0:
		default_branch = clampi(default_branch, 0, branches.size() - 1)
	return {
		"type": String(entry["type"]),
		"lambda": float(entry.get("lambda", 0.0)),            # events per real second
		"requires_decision": bool(entry.get("requires_decision", false)),
		"timer_seconds": float(entry.get("timer_seconds", 0.0)),
		"auto_resolve_role": _parse_role(String(entry.get("auto_resolve_role", "Maintenance"))),
		"expire_delta": _parse_delta(entry.get("faction_delta_on_expire", {})),
		"tile_trigger": String(entry.get("tile_trigger", "")),
		"branches": branches,
		"default_branch": default_branch,
	}


func _parse_delta(obj: Variant) -> Dictionary:
	var delta := {}
	if obj is Dictionary:
		for faction in C.FACTIONS:
			if obj.has(faction):
				delta[faction] = float(obj[faction])
	return delta


func _parse_role(name: String) -> int:
	var idx := C.ROLE_NAMES.find(name)
	return idx if idx >= 0 else C.Role.MAINTENANCE


# ── VehicleTypes.json ────────────────────────────────────────────

func _load_vehicles() -> void:
	var root: Variant = _read_json("VehicleTypes.json")
	if not (root is Array):
		return
	for entry in root:
		if not (entry is Dictionary) or not entry.has("type"):
			continue
		var type_name := String(entry["type"])
		var stalls: Array = []
		for s in entry.get("stall_requirements", []):
			var t := C.tile_from_name(String(s))
			if t >= 0:
				stalls.append(t)
		if stalls.is_empty():
			stalls = C.DEFAULT_STALL_RULES.get(type_name, [C.Tile.STALL_STANDARD])
		vehicle_defs[type_name] = {
			"type": type_name,
			"stalls": stalls,
			"speed": float(entry.get("speed_modifier", 1.0)),
			"weight": float(entry.get("spawn_weight", 0.0)),
			"revenue_mod": float(entry.get("revenue_modifier", 1.0)),
			"park_min": int(entry.get("park_duration_min_ticks", 400)),
			"park_max": int(entry.get("park_duration_max_ticks", 4800)),
			"incident_trigger": bool(entry.get("is_incident_trigger", false)),
		}


# ── CityEvents.json ──────────────────────────────────────────────

func _load_city_events() -> void:
	var root: Variant = _read_json("CityEvents.json")
	if not (root is Array):
		return
	for entry in root:
		if not (entry is Dictionary) or not entry.has("type"):
			continue
		city_event_defs.append({
			"type": String(entry["type"]),
			"scheduling": String(entry.get("scheduling", "")),
			"lambda": float(entry.get("lambda", 0.0)),         # per-tick start chance
			"demand_multiplier": float(entry.get("demand_multiplier", 1.0)),
			"duration_ticks": int(entry.get("duration_ticks", 0)),
			"trigger_faction": String(entry.get("trigger_faction", "")),
			"trigger_threshold": float(entry.get("trigger_threshold", 0.0)),
			"surge_pricing": bool(entry.get("surge_pricing_active", false)),
			"spike_vehicles": int(entry.get("demand_spike_vehicles", 0)),
			"spike_window_ticks": int(entry.get("spike_window_ticks", 0)),
			"power_loss_chance": float(entry.get("partial_power_loss_chance", 0.0)),
			"notes": String(entry.get("operational_notes", "")),
		})


# ── VehicleArrivals.json ─────────────────────────────────────────

func _load_arrivals() -> void:
	var root: Variant = _read_json("VehicleArrivals.json")
	if not (root is Dictionary):
		return
	for v in root.get("weekday", []):
		weekday_curve.append(float(v))
	for v in root.get("weekend", []):
		weekend_curve.append(float(v))
	if weekday_curve.size() != 24:
		errors.append("VehicleArrivals.json weekday curve must have 24 entries (has %d)" % weekday_curve.size())
