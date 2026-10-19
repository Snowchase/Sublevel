extends RefCounted
## SUBLEVEL simulation — the Godot port of USimulationSubsystem plus the
## City and Economy subsystems. Pure logic, no nodes: the game scene calls
## tick() at 20Hz × speed, and headless tests call it as fast as possible.
## Every random draw goes through `rng`, so a seed fully determines a run.

const C = preload("res://scripts/sim/sim_const.gd")
const DataLoader = preload("res://scripts/sim/data_loader.gd")
const FloorGrid = preload("res://scripts/sim/floor_grid.gd")
const Vehicle = preload("res://scripts/sim/vehicle.gd")
const Staff = preload("res://scripts/sim/staff.gd")
const Incident = preload("res://scripts/sim/incident.gd")

const MAX_MESSAGES := 60
const MAX_STAFF := 12

signal message_logged(text: String, kind: String)
signal decision_required(incident: RefCounted)
signal day_ended(summary: Dictionary)

# ── Core state ───────────────────────────────────────────────────
var data: DataLoader
var grid: FloorGrid
var rng := RandomNumberGenerator.new()
var seed_value := 0
var tick_count := 0
var day := 1
var setup_errors: Array[String] = []

var vehicles: Dictionary = {}        # id -> vehicle.gd
var staff: Dictionary = {}           # id -> staff.gd
var incidents: Dictionary = {}       # id -> incident.gd
var _next_vehicle_id := 1
var _next_staff_id := 1
var _next_incident_id := 1

# ── City ─────────────────────────────────────────────────────────
var demand_scale := 1.0              # playtest knob, adjustable in-game
var city_events_enabled := true      # off in the tutorial (scripted, no surprises)
var current_lambda := 0.0            # vehicles per real minute, after events + scale
var active_events: Array = []        # [{ "def": Dictionary, "start": int, "end": int }]
var _event_cooldown_until: Dictionary = {}
var rain_active := false
var outside_queue: Array = []        # [{ "type": String, "since": int }]
var _spawn_accum := 0.0
var _spike_remaining := 0
var _spike_end_tick := 0
var _spike_accum := 0.0
var _next_incident_tick: Dictionary = {}   # type -> tick

# ── Economy & factions ───────────────────────────────────────────
var balance := C.STARTING_BALANCE
var loan_principal := 0.0
var reputation := C.REPUTATION_START   # 0-100, scales demand (playtest mechanic)
var _gate_busy := 0                    # ticks until the entry gate takes the next car
var factions: Dictionary = {"city_authority": 0.0, "police": 0.0, "shadow_clients": 0.0}
var bankrupt := false
var goal_reached := false

# ── Telemetry ────────────────────────────────────────────────────
var stats: Dictionary = {}           # current day, reset at day end
var day_history: Array = []          # one summary per finished day
var decision_log: Array = []         # every decision (made or expired)
var messages: Array = []             # recent log lines [{tick, text, kind}]


# ═════════════════════════════════════════════════════════════════
# SETUP
# ═════════════════════════════════════════════════════════════════

## Builds a fresh session. Pass a pre-loaded data_loader to share parsed
## JSON between many sims (tests); otherwise Data/ is read from disk.
func setup(session_seed: int, shared_data: DataLoader = null, lot_size: Vector2i = C.START_LOT_SIZE) -> bool:
	seed_value = session_seed
	rng.seed = session_seed

	if shared_data != null:
		data = shared_data
	else:
		data = DataLoader.new()
		data.load_all()
	setup_errors.assign(data.errors)

	grid = FloorGrid.new()
	grid.generate_default_layout(lot_size.x, lot_size.y)

	tick_count = C.START_HOUR * C.TICKS_PER_HOUR
	day = 1
	_reset_day_stats()

	for type_name in data.incident_defs.keys():
		_schedule_next_incident(type_name)

	return setup_errors.is_empty()


func hour_of_day() -> int:
	return C.tick_to_hour(tick_count)


func clock_text() -> String:
	return C.tick_to_clock(tick_count)


# ═════════════════════════════════════════════════════════════════
# TICK
# ═════════════════════════════════════════════════════════════════

func tick() -> void:
	tick_count += 1
	if tick_count % C.TICKS_PER_DAY == 0:
		_end_day()

	_update_demand()
	if city_events_enabled:
		_tick_city_events()
	_tick_arrivals()
	_tick_incidents()
	_dispatch_staff()

	for v in vehicles.values():
		v.tick(self)
	for id in vehicles.keys():
		if vehicles[id].state == C.VehicleState.DONE:
			vehicles.erase(id)

	var idle_maintenance := 0
	for s in staff.values():
		s.tick(self)
		if s.role == C.Role.MAINTENANCE and s.is_idle():
			idle_maintenance += 1

	if tick_count % C.CONGESTION_UPDATE_TICKS == 0:
		grid.update_congestion_weights()
		sample_occupancy()
	grid.tick_integrity(idle_maintenance)

	_tick_economy()


# ═════════════════════════════════════════════════════════════════
# CITY — demand curve, events, arrivals (UCitySubsystem)
# ═════════════════════════════════════════════════════════════════

func is_weekend() -> bool:
	return ((day - 1) % 7) >= 5


func _update_demand() -> void:
	var hour := hour_of_day()
	var curve: Array = data.weekday_curve
	if is_weekend() and data.weekend_curve.size() == 24:
		curve = data.weekend_curve
	var base := 2.0
	if hour < curve.size():
		base = float(curve[hour])
	var multiplier := 1.0
	for e in active_events:
		multiplier *= maxf(float(e["def"]["demand_multiplier"]), 0.0)
	current_lambda = base * C.DEMAND_CURVE_SCALE * multiplier * demand_scale * reputation_multiplier()


## Demand multiplier from reputation: 0 → ×0.6, 50 → ×1.0, 100 → ×1.4.
func reputation_multiplier() -> float:
	return lerpf(C.REP_DEMAND_MIN, C.REP_DEMAND_MAX, reputation / 100.0)


func change_reputation(delta: float) -> void:
	reputation = clampf(reputation + delta, 0.0, 100.0)


func event_active(type_name: String) -> bool:
	for e in active_events:
		if e["def"]["type"] == type_name:
			return true
	return false


func surge_active() -> bool:
	for e in active_events:
		if e["def"]["surge_pricing"]:
			return true
	return false


func _tick_city_events() -> void:
	for i in range(active_events.size() - 1, -1, -1):
		if tick_count >= int(active_events[i]["end"]):
			_end_event(i)

	var hour_boundary := tick_count % C.TICKS_PER_HOUR == 0
	var hour := hour_of_day()

	for def in data.city_event_defs:
		var type_name: String = def["type"]
		if event_active(type_name):
			continue
		if tick_count < int(_event_cooldown_until.get(type_name, 0)):
			continue

		var start := false
		match String(def["scheduling"]):
			"stochastic":
				start = rng.randf() < float(def["lambda"])
			"scheduled":
				if hour_boundary:
					if type_name == "StadiumNight":
						start = hour == 19 and day % 3 == 0
					elif type_name == "ConcertLetout":
						start = hour == 22 and day % 2 == 0
			"faction_triggered":
				if hour_boundary:
					var score := float(factions.get(String(def["trigger_faction"]), 0.0))
					var threshold := float(def["trigger_threshold"])
					start = score >= threshold if threshold >= 0.0 else score <= threshold
		if start:
			_start_event(def)


func _start_event(def: Dictionary) -> void:
	var type_name: String = def["type"]
	active_events.append({"def": def, "start": tick_count, "end": tick_count + int(def["duration_ticks"])})
	stats["city_events"].append(type_name)

	if type_name == "Rainstorm":
		rain_active = true
	if int(def["spike_vehicles"]) > 0:
		var share := float(grid.stalls.size()) / C.EVENT_SPIKE_REFERENCE_STALLS
		_spike_remaining = maxi(1, roundi(int(def["spike_vehicles"]) * minf(share, 1.0)))
		_spike_end_tick = tick_count + maxi(int(def["spike_window_ticks"]), 1)
		_spike_accum = 0.0
	if type_name == "PowerGridStrain" and rng.randf() < float(def["power_loss_chance"]):
		spawn_incident("PowerOutage")
	if type_name == "CityInspection":
		if grid.integrity < 0.5:
			_spend(C.INSPECTION_FINE, "fines")
			apply_faction("city_authority", -5.0)
			log_message("City inspection FAILED — structure at %d%%. Fined $%d." % [int(grid.integrity * 100), int(C.INSPECTION_FINE)], "bad")
		else:
			apply_faction("city_authority", 2.0)
			log_message("City inspection passed.", "good")

	log_message("City event: %s — %s" % [type_name, def["notes"]], "event")


func _end_event(index: int) -> void:
	var e: Dictionary = active_events[index]
	var type_name: String = e["def"]["type"]
	active_events.remove_at(index)
	_event_cooldown_until[type_name] = tick_count + int(e["def"]["duration_ticks"]) * 2
	if type_name == "Rainstorm":
		rain_active = event_active("Rainstorm")
	log_message("City event ended: %s" % type_name, "event")


func _tick_arrivals() -> void:
	# λ is vehicles per real minute (UE: CurrentLambda / 60 per second).
	_spawn_accum += current_lambda / 60.0 * C.TICK_SECONDS

	# ConcertLetout-style burst spread over its spike window.
	if _spike_remaining > 0 and tick_count <= _spike_end_tick:
		var ticks_left := _spike_end_tick - tick_count + 1
		_spike_accum += float(_spike_remaining) / float(ticks_left)
		while _spike_accum >= 1.0 and _spike_remaining > 0:
			_spike_accum -= 1.0
			_spike_remaining -= 1
			_spawn_accum += 1.0

	while _spawn_accum >= 1.0:
		_spawn_accum -= 1.0
		if outside_queue.size() >= C.OUTSIDE_QUEUE_MAX:
			stats["lost_at_gate"] += 1
			change_reputation(C.REP_LOST_AT_GATE)
		else:
			outside_queue.append({"type": _pick_vehicle_type(), "since": tick_count})
		stats["arrivals"] += 1

	# Impatient cars on the street give up.
	while not outside_queue.is_empty() and tick_count - int(outside_queue[0]["since"]) > C.OUTSIDE_QUEUE_PATIENCE:
		outside_queue.pop_front()
		stats["lost_at_gate"] += 1
		change_reputation(C.REP_LOST_AT_GATE)

	# The entry gate processes one car at a time; attendants make it faster.
	if _gate_busy > 0:
		_gate_busy -= 1
	elif not outside_queue.is_empty() and grid.vehicles_on(grid.entry_gate) < C.MAX_VEHICLES_PER_TILE:
		var next: Dictionary = outside_queue.pop_front()
		spawn_vehicle(String(next["type"]))
		_gate_busy = gate_ticks_per_car()


## Ticks the entry gate needs per car: slow without an attendant.
func gate_ticks_per_car() -> int:
	var attendants := count_staff(C.Role.ATTENDANT)
	return int(C.GATE_TICKS[mini(attendants, C.GATE_TICKS.size() - 1)])


func _pick_vehicle_type() -> String:
	var shadow := float(factions["shadow_clients"])
	var total := 0.0
	for def in data.vehicle_defs.values():
		total += _spawn_weight(def, shadow)
	if total <= 0.0:
		return "Standard"
	var roll := rng.randf() * total
	for def in data.vehicle_defs.values():
		roll -= _spawn_weight(def, shadow)
		if roll <= 0.0:
			return String(def["type"])
	return String(data.vehicle_defs.values().back()["type"])


func _spawn_weight(def: Dictionary, shadow: float) -> float:
	var w := float(def["weight"])
	if def["incident_trigger"] and shadow >= 20.0:
		w *= 3.0
	return w


func spawn_vehicle(type_name: String) -> Vehicle:
	var def: Dictionary = data.vehicle_defs.get(type_name, {})
	if def.is_empty():
		def = {"type": type_name, "stalls": C.DEFAULT_STALL_RULES.get(type_name, [C.Tile.STALL_STANDARD]),
			"speed": 1.0, "weight": 0.0, "revenue_mod": 1.0, "park_min": 400, "park_max": 4800,
			"incident_trigger": false}
	var v := Vehicle.new()
	v.setup(_next_vehicle_id, def, grid.entry_gate, tick_count)
	_next_vehicle_id += 1
	grid.add_vehicle(v.cell)
	vehicles[v.id] = v
	stats["entered"] += 1
	return v


# ═════════════════════════════════════════════════════════════════
# VEHICLE CALLBACKS
# ═════════════════════════════════════════════════════════════════

func on_vehicle_parked(v: Vehicle) -> void:
	stats["parked"] += 1
	stats["seek_ticks_total"] += tick_count - v.spawn_tick


func on_vehicle_exited(v: Vehicle) -> void:
	var hours := maxf(float(tick_count - v.park_start) / C.TICKS_PER_HOUR, C.MIN_BILLED_HOURS)
	var fee := hours * C.HOURLY_RATE * float(v.def.get("revenue_mod", 1.0))
	if surge_active():
		fee *= C.SURGE_MULTIPLIER
	_earn(fee, "parking")
	stats["served"] += 1
	change_reputation(C.REP_SERVED)


func on_vehicle_left_unserved(_v: Vehicle) -> void:
	stats["turned_away"] += 1
	change_reputation(C.REP_TURNED_AWAY)


func parked_vehicles() -> Array:
	var out: Array = []
	for v in vehicles.values():
		if v.is_parked():
			out.append(v)
	return out


# ═════════════════════════════════════════════════════════════════
# INCIDENTS (§5)
# ═════════════════════════════════════════════════════════════════

## Effective incident rate (events per real second): the JSON rate scaled
## down for smaller lots, and for crime types, by Security guards on staff.
func incident_rate(type_name: String) -> float:
	var lam := float(data.incident_defs[type_name]["lambda"])
	var stall_count := maxf(float(grid.stalls.size()), 1.0)
	lam *= pow(stall_count / C.INCIDENT_REFERENCE_STALLS, C.INCIDENT_SIZE_EXPONENT)
	if type_name in C.CRIME_INCIDENTS:
		var guards := count_staff(C.Role.SECURITY)
		lam *= maxf(pow(C.SECURITY_CRIME_FACTOR, guards), C.SECURITY_CRIME_FLOOR)
	return lam


func _schedule_next_incident(type_name: String) -> void:
	var lam := incident_rate(type_name)
	if lam <= 0.0:
		return
	var u := maxf(rng.randf(), 0.000001)
	var seconds := -log(u) / lam
	_next_incident_tick[type_name] = tick_count + maxi(1, int(round(seconds / C.TICK_SECONDS)))


func _tick_incidents() -> void:
	for type_name in _next_incident_tick.keys():
		if tick_count >= int(_next_incident_tick[type_name]):
			spawn_incident(type_name)
			_schedule_next_incident(type_name)

	for inc in incidents.values():
		match inc.state:
			C.IncidentState.DECISION_PENDING:
				if tick_count >= inc.deadline_tick:
					_expire_decision(inc)
			C.IncidentState.ACTIVE:
				if inc.staff_id == 0 and tick_count - inc.spawn_tick >= C.AUTO_INCIDENT_TIMEOUT:
					_expire_auto_incident(inc)

	# Drop closed incidents after a while (keeps the HUD history short).
	for id in incidents.keys():
		var inc = incidents[id]
		if not inc.is_open() and tick_count - inc.closed_tick > 2000:
			incidents.erase(id)


## Creates an incident now. `forced_cell` pins it to a tile (tests).
func spawn_incident(type_name: String, forced_cell: Vector2i = C.NO_CELL) -> Incident:
	var def: Dictionary = data.incident_defs.get(type_name, {})
	if def.is_empty():
		return null

	var inc := Incident.new()
	inc.type_name = type_name
	inc.def = def
	inc.spawn_tick = tick_count
	inc.severity = rng.randf_range(0.3, 1.0)

	if forced_cell != C.NO_CELL:
		inc.cell = forced_cell
	elif type_name in C.VEHICLE_INCIDENTS:
		var parked := parked_vehicles()
		if not parked.is_empty():
			var target = parked[rng.randi_range(0, parked.size() - 1)]
			inc.cell = target.cell
			inc.vehicle_id = target.id
	if inc.cell == C.NO_CELL:
		inc.cell = grid.random_lane_cell(rng)
	if inc.cell == C.NO_CELL:
		return null

	inc.id = _next_incident_id
	_next_incident_id += 1

	if type_name in C.BLOCKING_INCIDENTS and not C.is_stall(grid.tile_at(inc.cell)):
		inc.blocking = true
		grid.set_blocked(inc.cell, true)
	if type_name == "StructuralCrack":
		grid.active_cracks += 1

	incidents[inc.id] = inc
	stats["incidents"] += 1

	if inc.requires_decision():
		inc.state = C.IncidentState.DECISION_PENDING
		inc.deadline_tick = tick_count + int(round(float(def["timer_seconds"]) / C.TICK_SECONDS))
		log_message("DECISION NEEDED: %s (%ds)" % [type_name, int(def["timer_seconds"])], "decision")
		decision_required.emit(inc)
	else:
		inc.state = C.IncidentState.ACTIVE
		log_message("Incident: %s" % type_name, "warn")
	return inc


## The most urgent open decision, or null.
func next_decision() -> Incident:
	var best = null
	for inc in incidents.values():
		if inc.state == C.IncidentState.DECISION_PENDING:
			if best == null or inc.deadline_tick < best.deadline_tick:
				best = inc
	return best


func pending_decision_count() -> int:
	var n := 0
	for inc in incidents.values():
		if inc.state == C.IncidentState.DECISION_PENDING:
			n += 1
	return n


## Branches whose label starts with "Security" need a Security guard on staff.
## (Playtest hypothesis — gives the Security role a purpose.)
func is_branch_available(inc: Incident, branch_index: int) -> bool:
	var branches: Array = inc.def.get("branches", [])
	if branch_index < 0 or branch_index >= branches.size():
		return false
	var label := String(branches[branch_index]["label"])
	if label.begins_with("Security"):
		return count_staff(C.Role.SECURITY) > 0
	return true


## Player picks a branch. Returns false if the choice isn't allowed.
func resolve_decision(incident_id: int, branch_index: int) -> bool:
	var inc = incidents.get(incident_id)
	if inc == null or inc.state != C.IncidentState.DECISION_PENDING:
		return false
	if not is_branch_available(inc, branch_index):
		return false
	_apply_branch(inc, branch_index, false)
	_close_incident(inc, C.IncidentState.RESOLVED)
	stats["decisions_made"] += 1
	return true


## Branch that fires if the card times out: the JSON default, or the first
## available branch when the default is locked (e.g. no Security on staff).
## -1 if none is available.
func expiry_branch(inc: Incident) -> int:
	var branch := int(inc.def.get("default_branch", 0))
	if is_branch_available(inc, branch):
		return branch
	for i in inc.def.get("branches", []).size():
		if is_branch_available(inc, i):
			return i
	return -1


func _expire_decision(inc: Incident) -> void:
	var branch := expiry_branch(inc)
	if branch >= 0:
		_apply_branch(inc, branch, true)
	_close_incident(inc, C.IncidentState.EXPIRED)
	stats["decisions_expired"] += 1
	change_reputation(C.REP_DECISION_IGNORED)


func _apply_branch(inc: Incident, branch_index: int, expired: bool) -> void:
	var branch: Dictionary = inc.def["branches"][branch_index]
	var delta: Dictionary = branch["delta"]
	for faction in delta.keys():
		apply_faction(faction, float(delta[faction]))
	_apply_consequence(inc, String(branch["consequence"]))

	var label := String(branch["label"])
	inc.outcome = label
	if label.begins_with("Security"):
		_send_security(inc)

	decision_log.append({
		"day": day, "clock": clock_text(), "incident": inc.type_name,
		"branch": label, "expired": expired,
		"seconds_left": inc.seconds_left(tick_count) if not expired else 0.0,
	})
	if expired:
		log_message("Ignored %s — default: %s" % [inc.type_name, label], "bad")
	else:
		log_message("%s → %s" % [inc.type_name, label], "good")


## Monetary side effects of branch consequence tags (UE: ApplyConsequence).
func _apply_consequence(inc: Incident, tag: String) -> void:
	var money := float(C.CONSEQUENCE_MONEY.get(tag, 0.0))
	if tag in C.TOW_CONSEQUENCES and not _tow_vehicle(inc.vehicle_id):
		money = 0.0   # nothing to tow (car already left) — nothing recovered
	if money > 0.0:
		_earn(money, "shadow" if tag == "vehicle_stays_shadow_credit" else "decisions")
	elif money < 0.0:
		_spend(-money, "losses")
	if tag == "theft_completed_revenue_lost":
		change_reputation(C.REP_THEFT)


## Returns true if a car was removed.
func _tow_vehicle(vehicle_id: int) -> bool:
	var v = vehicles.get(vehicle_id)
	if v == null or v.state == C.VehicleState.DONE:
		return false
	v.tow(self)
	vehicles.erase(vehicle_id)   # gone now, not next tick (matters while paused)
	stats["towed"] += 1
	return true


## Puts an incident back in the dispatch queue with a fresh unattended
## clock (its worker was let go or couldn't reach it).
func requeue_incident(inc: Incident) -> void:
	inc.state = C.IncidentState.ACTIVE
	inc.staff_id = 0
	inc.spawn_tick = tick_count


func _expire_auto_incident(inc: Incident) -> void:
	var delta: Dictionary = inc.def.get("expire_delta", {})
	for faction in delta.keys():
		apply_faction(faction, float(delta[faction]))
	_spend(C.AUTO_INCIDENT_EXPIRE_FINE, "fines")
	inc.outcome = "Unattended — city cleaned up, fined"
	_close_incident(inc, C.IncidentState.EXPIRED)
	stats["incidents_unattended"] += 1
	change_reputation(C.REP_UNATTENDED)
	log_message("%s left unattended. City fined you $%d." % [inc.type_name, int(C.AUTO_INCIDENT_EXPIRE_FINE)], "bad")


func resolve_by_staff(incident_id: int, by: Staff) -> void:
	var inc = incidents.get(incident_id)
	if inc == null or not inc.is_open():
		return
	inc.outcome = "Handled by %s" % by.display_name
	_close_incident(inc, C.IncidentState.RESOLVED)
	stats["resolved_by_staff"] += 1
	log_message("%s cleared the %s." % [by.display_name, inc.type_name], "good")


func _close_incident(inc: Incident, final_state: int) -> void:
	inc.state = final_state
	inc.closed_tick = tick_count
	if inc.blocking:
		inc.blocking = false
		var still_blocked := false
		for other in incidents.values():
			if other != inc and other.blocking and other.cell == inc.cell:
				still_blocked = true
		if not still_blocked:
			grid.set_blocked(inc.cell, false)
	if inc.type_name == "StructuralCrack":
		grid.active_cracks = maxi(0, grid.active_cracks - 1)


# ═════════════════════════════════════════════════════════════════
# STAFF (§6)
# ═════════════════════════════════════════════════════════════════

func hire_staff(role: int) -> Staff:
	if staff.size() >= MAX_STAFF:
		log_message("Staff roster is full (%d)." % MAX_STAFF, "warn")
		return null
	var s := Staff.new()
	var trait_id := rng.randi_range(0, C.TRAIT_NAMES.size() - 1)
	# Start just inside the entrance, not on the gate tile (keeps the IN label visible).
	var start := grid.entry_gate + Vector2i(0, 1)
	if not grid.is_passable(start):
		start = grid.entry_gate
	s.setup(_next_staff_id, role, trait_id, start)
	_next_staff_id += 1
	staff[s.id] = s
	log_message("Hired %s (%s). Wage $%d/day." % [s.display_name, C.TRAIT_NAMES[trait_id], int(C.STAFF_WAGES[role])], "good")
	return s


## Lets one staff member of `role` go — the most recently hired idle one
## if possible. Returns true if someone was let go.
func fire_staff(role: int) -> bool:
	var pick = null
	for s in staff.values():
		if s.role == role and (pick == null or s.is_idle()):
			pick = s
	if pick == null:
		return false
	if not pick.is_idle() and pick.task["kind"] == "resolve":
		var inc = incidents.get(pick.task["incident_id"])
		if inc != null and inc.is_open():
			requeue_incident(inc)
	staff.erase(pick.id)
	log_message("Let go %s." % pick.display_name, "warn")
	return true


func count_staff(role: int) -> int:
	var n := 0
	for s in staff.values():
		if s.role == role:
			n += 1
	return n


func _dispatch_staff() -> void:
	for inc in incidents.values():
		if inc.state != C.IncidentState.ACTIVE or inc.staff_id != 0:
			continue
		var role := int(inc.def.get("auto_resolve_role", C.Role.MAINTENANCE))
		var best = null
		var best_dist := 1 << 30
		for s in staff.values():
			if s.role != role or not s.is_idle():
				continue
			var d: int = absi(s.cell.x - inc.cell.x) + absi(s.cell.y - inc.cell.y)
			if d < best_dist:
				best_dist = d
				best = s
		if best == null:
			continue
		var work := int(ceil(C.STAFF_WORK_TICKS / best.resolution_speed()))
		best.assign("resolve", inc.id, inc.cell, work)
		inc.staff_id = best.id
		inc.state = C.IncidentState.IN_PROGRESS


func _send_security(inc: Incident) -> void:
	var best = null
	var best_dist := 1 << 30
	for s in staff.values():
		if s.role != C.Role.SECURITY or not s.is_idle():
			continue
		var d: int = absi(s.cell.x - inc.cell.x) + absi(s.cell.y - inc.cell.y)
		if d < best_dist:
			best_dist = d
			best = s
	if best != null:
		best.assign("respond", inc.id, inc.cell, 100)


# ═════════════════════════════════════════════════════════════════
# ECONOMY & FACTIONS (§10)
# ═════════════════════════════════════════════════════════════════

func apply_faction(faction: String, delta: float) -> void:
	if not factions.has(faction):
		return
	factions[faction] = clampf(float(factions[faction]) + delta, -100.0, 100.0)


## Returns false when the bank refuses (principal would pass the cap).
func take_loan() -> bool:
	if loan_principal + C.LOAN_AMOUNT > C.MAX_LOAN_PRINCIPAL:
		log_message("The bank refuses: you already owe $%d (limit $%d)." % [int(loan_principal), int(C.MAX_LOAN_PRINCIPAL)], "bad")
		return false
	balance += C.LOAN_AMOUNT
	loan_principal += C.LOAN_AMOUNT
	stats["loans_taken"] += 1
	log_message("Took a $%d loan (principal now $%d)." % [int(C.LOAN_AMOUNT), int(loan_principal)], "warn")
	return true


func _earn(amount: float, category: String) -> void:
	balance += amount
	stats["revenue"] += amount
	stats["revenue_by"][category] = float(stats["revenue_by"].get(category, 0.0)) + amount


func _spend(amount: float, category: String) -> void:
	balance -= amount
	stats["expenses"] += amount
	stats["expenses_by"][category] = float(stats["expenses_by"].get(category, 0.0)) + amount


func _tick_economy() -> void:
	if event_active("BlackMarketNight") and not event_active("PoliceSweep"):
		_earn(C.SHADOW_INCOME_PER_TICK, "shadow")


func _end_day() -> void:
	_spend(C.DAILY_RENT, "rent")

	var wages := 0.0
	for s in staff.values():
		wages += float(C.STAFF_WAGES[s.role])
	if wages > 0.0:
		_spend(wages, "wages")

	if loan_principal > 0.0:
		var interest := loan_principal * C.LOAN_INTEREST_RATE
		var principal_part := minf(loan_principal, loan_principal * C.LOAN_REPAYMENT_FRACTION)
		_spend(interest + principal_part, "loan")
		loan_principal -= principal_part
		if loan_principal < 1.0:
			loan_principal = 0.0

	for f in C.FACTIONS:
		factions[f] = float(factions[f]) * (1.0 - C.FACTION_DAILY_DECAY)

	var summary := stats.duplicate(true)
	summary["day"] = day
	summary["reputation"] = reputation
	summary["stall_count"] = grid.stalls.size()
	summary["avg_occupancy"] = (float(stats["occupancy_sum"]) / stats["occupancy_samples"]) if stats["occupancy_samples"] > 0 else 0.0
	summary["balance_end"] = balance
	summary["loan_principal"] = loan_principal
	summary["factions"] = factions.duplicate()
	summary["integrity"] = grid.integrity
	summary["staff"] = {
		"attendant": count_staff(C.Role.ATTENDANT),
		"security": count_staff(C.Role.SECURITY),
		"maintenance": count_staff(C.Role.MAINTENANCE),
	}
	summary["avg_seek_seconds"] = (float(stats["seek_ticks_total"]) / stats["parked"] * C.TICK_SECONDS) if stats["parked"] > 0 else 0.0
	day_history.append(summary)

	log_message("Day %d closed: revenue $%d, expenses $%d, balance $%d." % [day, int(stats["revenue"]), int(stats["expenses"]), int(balance)], "event")
	day_ended.emit(summary)

	day = int(tick_count / C.TICKS_PER_DAY) + 1
	_reset_day_stats()

	if balance < C.BANKRUPT_THRESHOLD and not bankrupt:
		bankrupt = true
		log_message("BANKRUPT — the bank has called in your loans.", "bad")
	if day > C.GOAL_DAYS and not goal_reached and not bankrupt:
		goal_reached = true
		log_message("Goal reached: you kept SUBLEVEL open for %d days!" % C.GOAL_DAYS, "good")


func _reset_day_stats() -> void:
	stats = {
		"arrivals": 0, "entered": 0, "parked": 0, "served": 0,
		"turned_away": 0, "lost_at_gate": 0, "towed": 0,
		"seek_ticks_total": 0, "peak_occupancy": 0,
		"incidents": 0, "resolved_by_staff": 0, "incidents_unattended": 0,
		"decisions_made": 0, "decisions_expired": 0,
		"revenue": 0.0, "expenses": 0.0,
		"revenue_by": {}, "expenses_by": {},
		"loans_taken": 0, "city_events": [],
		"occupancy_sum": 0, "occupancy_samples": 0,
	}


func occupied_stalls() -> int:
	return grid.stalls.size() - grid.count_free_stalls()


## Called by the sim every CONGESTION_UPDATE_TICKS to track peak and
## average occupancy. UI code should use occupied_stalls() instead.
func sample_occupancy() -> int:
	var occupied := occupied_stalls()
	stats["occupancy_sum"] += occupied
	stats["occupancy_samples"] += 1
	if occupied > int(stats["peak_occupancy"]):
		stats["peak_occupancy"] = occupied
	return occupied


# ═════════════════════════════════════════════════════════════════
# LOG & DIAGNOSTICS
# ═════════════════════════════════════════════════════════════════

func log_message(text: String, kind: String = "info") -> void:
	messages.append({"tick": tick_count, "text": text, "kind": kind})
	if messages.size() > MAX_MESSAGES:
		messages.pop_front()
	message_logged.emit(text, kind)


## Compact fingerprint of the full sim state — equal seeds must produce
## equal signatures (determinism test).
func state_signature() -> String:
	var parts: PackedStringArray = []
	parts.append("t%d d%d b%.4f l%.2f r%.3f" % [tick_count, day, balance, loan_principal, reputation])
	for f in C.FACTIONS:
		parts.append("%s%.3f" % [f, factions[f]])
	for v in vehicles.values():
		parts.append("v%d:%s:%d,%d:%d" % [v.id, v.type_name, v.cell.x, v.cell.y, v.state])
	for s in staff.values():
		parts.append("s%d:%d,%d:%.4f" % [s.id, s.cell.x, s.cell.y, s.fatigue])
	for inc in incidents.values():
		parts.append("i%d:%s:%d,%d:%d" % [inc.id, inc.type_name, inc.cell.x, inc.cell.y, inc.state])
	parts.append("q%d" % outside_queue.size())
	return "|".join(parts).md5_text()
