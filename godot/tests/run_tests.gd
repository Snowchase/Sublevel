extends SceneTree
## SUBLEVEL playtest — headless test suite.
##
##   godot --headless --path godot --script res://tests/run_tests.gd
##   godot --headless --path godot --script res://tests/run_tests.gd -- --only=test_layout
##
## Exit code 0 = all passed, 1 = failures. Expected values come from
## Data/*.json, so if you retune the data some assertions may need updating.

const C = preload("res://scripts/sim/sim_const.gd")
const Simulation = preload("res://scripts/sim/simulation.gd")
const DataLoader = preload("res://scripts/sim/data_loader.gd")

const TESTS := [
	"test_data_loads",
	"test_layout",
	"test_pathing",
	"test_determinism",
	"test_single_vehicle_lifecycle",
	"test_stall_compatibility",
	"test_decision_branch_applies_json_deltas",
	"test_decision_expiry_fires_default_branch",
	"test_security_gated_branches",
	"test_staff_clears_blocking_incident",
	"test_unattended_incident_is_fined",
	"test_tow_consequence_removes_vehicle",
	"test_day_end_wages_and_loan",
	"test_bankruptcy",
	"test_city_events",
	"test_street_queue_overflow",
	"test_hud_and_main_scene_smoke",
	"test_soak_three_days",
]

var data: DataLoader
var _passed := 0
var _failed := 0
var _failures: Array[String] = []
var _current := ""


var _ran := false


func _initialize() -> void:
	print("\n=== SUBLEVEL playtest — headless tests ===")
	data = DataLoader.new()
	data.load_all()
	print("Data dir: %s" % data.data_dir)


## Tests run on the first frame: by then the scene tree is live, so nodes
## added to `root` receive _ready immediately (they don't inside _initialize).
func _process(_delta: float) -> bool:
	if _ran:
		return false
	_ran = true
	_run_all()
	return false


func _run_all() -> void:
	var only := ""
	for arg in OS.get_cmdline_user_args():
		if String(arg).begins_with("--only="):
			only = String(arg).substr(7)

	var started := Time.get_ticks_msec()
	for test_name in TESTS:
		if only != "" and test_name != only:
			continue
		_current = test_name
		var failed_before := _failed
		var t0 := Time.get_ticks_msec()
		var completed: Variant = call(test_name)
		if not (completed is bool and completed):
			_failed += 1
			_failures.append("%s: aborted by a script error (see SCRIPT ERROR above)" % test_name)
		var ms := Time.get_ticks_msec() - t0
		print("%s  %s  (%d ms)" % ["PASS" if _failed == failed_before else "FAIL", test_name, ms])

	print("\n%d checks passed, %d failed  (%.1f s)" % [_passed, _failed, (Time.get_ticks_msec() - started) / 1000.0])
	if _failed > 0:
		print("\nFailures:")
		for f in _failures:
			print("  - " + f)
	quit(1 if _failed > 0 else 0)


# ── Helpers ──────────────────────────────────────────────────────

func check(condition: bool, message: String) -> void:
	if condition:
		_passed += 1
	else:
		_failed += 1
		_failures.append("%s: %s" % [_current, message])
		print("    x " + message)


func approx(a: float, b: float, eps: float = 0.01) -> bool:
	return absf(a - b) <= eps


## A sim with no random arrivals or incidents — tests add what they need.
func quiet_sim(seed_value: int = 1234) -> Simulation:
	var sim := Simulation.new()
	sim.setup(seed_value, data)
	sim.demand_scale = 0.0
	sim._next_incident_tick.clear()
	return sim


func run(sim: Simulation, ticks: int) -> void:
	for _i in ticks:
		sim.tick()


## Ticks until `predicate` returns true or `max_ticks` pass. Returns ticks used, -1 on timeout.
func run_until(sim: Simulation, predicate: Callable, max_ticks: int) -> int:
	for i in max_ticks:
		if predicate.call():
			return i
		sim.tick()
	return -1 if not predicate.call() else max_ticks


# ── Tests ────────────────────────────────────────────────────────

func test_data_loads() -> bool:
	check(data.errors.is_empty(), "data errors: %s" % str(data.errors))
	check(data.incident_defs.size() == 10, "10 incident types (got %d)" % data.incident_defs.size())
	check(data.vehicle_defs.size() == 6, "6 vehicle types (got %d)" % data.vehicle_defs.size())
	check(data.city_event_defs.size() == 8, "8 city events (got %d)" % data.city_event_defs.size())
	check(data.weekday_curve.size() == 24 and data.weekend_curve.size() == 24, "24-hour demand curves")

	var overstay: Dictionary = data.incident_defs.get("OverstayVehicle", {})
	check(overstay.get("branches", []).size() == 3, "OverstayVehicle has 3 branches")
	check(int(overstay.get("default_branch", -1)) == 1, "OverstayVehicle default branch is 1")
	var suspicious: Dictionary = data.incident_defs.get("SuspiciousVehicle", {})
	var delta: Dictionary = suspicious["branches"][0]["delta"]
	check(approx(delta.get("police", 0.0), 5.0) and approx(delta.get("shadow_clients", 0.0), -10.0),
		"SuspiciousVehicle 'Report to Police' delta = police +5, shadow -10")
	var suv: Dictionary = data.vehicle_defs.get("SUV", {})
	check(suv.get("stalls", []) == [C.Tile.STALL_OVERSIZED], "SUV parks only in oversized stalls")
	return true


func test_layout() -> bool:
	var sim := quiet_sim()
	var grid = sim.grid
	check(grid.width == 24 and grid.height == 16, "24x16 grid")
	check(grid.stalls.size() == 144, "144 stalls (got %d)" % grid.stalls.size())
	check(grid.entry_gate == Vector2i(6, 0), "entry gate at (6,0)")
	check(grid.exit_gate == Vector2i(18, 0), "exit gate at (18,0)")
	check(grid.unreachable_stalls.is_empty(), "every stall reachable (%d unreachable)" % grid.unreachable_stalls.size())

	var counts := {}
	for s in grid.stalls:
		var t: int = grid.tile_at(s)
		counts[t] = int(counts.get(t, 0)) + 1
	check(counts.get(C.Tile.STALL_COMPACT, 0) == 32, "32 compact stalls")
	check(counts.get(C.Tile.STALL_STANDARD, 0) == 72, "72 standard stalls")
	check(counts.get(C.Tile.STALL_OVERSIZED, 0) == 24, "24 oversized stalls")
	check(counts.get(C.Tile.STALL_MOTORCYCLE, 0) == 8, "8 motorcycle stalls")
	check(counts.get(C.Tile.STALL_DISABLED, 0) == 8, "8 disabled stalls")
	return true


func test_pathing() -> bool:
	var sim := quiet_sim()
	var grid = sim.grid

	var through: Array[Vector2i] = grid.find_path(grid.entry_gate, grid.exit_gate)
	check(not through.is_empty(), "entry → exit path exists")
	check(not through.is_empty() and through.back() == grid.exit_gate, "path ends at the exit gate")
	var all_drivable := true
	for cell in through:
		if not grid.is_passable(cell):
			all_drivable = false
	check(all_drivable, "entry → exit path only uses drivable tiles")

	var stall := Vector2i(10, 4)
	var to_stall: Array[Vector2i] = grid.find_path(grid.entry_gate, stall)
	check(not to_stall.is_empty() and to_stall.back() == stall, "path into a stall ends in the stall")
	if to_stall.size() >= 2:
		check(grid.is_passable(to_stall[to_stall.size() - 2]), "stall is entered from a lane, not through other stalls")
	check(not grid.astar.is_point_solid(grid.entry_gate), "astar solid flags restored after query")
	check(grid.astar.is_point_solid(stall), "stall stays solid for through-traffic")

	# Block a tile on the shortest route — the new route must avoid it.
	var blocker: Vector2i = through[through.size() / 2]
	grid.set_blocked(blocker, true)
	var rerouted: Array[Vector2i] = grid.find_path(grid.entry_gate, grid.exit_gate)
	check(not rerouted.is_empty(), "a route still exists with one tile blocked")
	check(not rerouted.has(blocker), "re-route avoids the blocked tile")
	grid.set_blocked(blocker, false)
	check(grid.is_passable(blocker), "unblocking restores the tile")
	return true


func test_determinism() -> bool:
	var a := Simulation.new()
	var b := Simulation.new()
	var c := Simulation.new()
	a.setup(42, data)
	b.setup(42, data)
	c.setup(43, data)
	for sim in [a, b, c]:
		sim.demand_scale = 3.0
		sim.hire_staff(C.Role.MAINTENANCE)
		sim.hire_staff(C.Role.SECURITY)
	for _i in 6000:
		a.tick()
		b.tick()
		c.tick()
	check(a.state_signature() == b.state_signature(), "same seed → identical state after 6000 ticks")
	check(a.state_signature() != c.state_signature(), "different seed → different state")
	check(a.vehicles.size() > 0 or a.stats["entered"] > 0, "determinism run actually had traffic")
	return true


func test_single_vehicle_lifecycle() -> bool:
	var sim := quiet_sim()
	var v = sim.spawn_vehicle("Standard")
	var ticks := run_until(sim, func(): return v.is_parked(), 600)
	check(ticks >= 0, "vehicle parks within 600 ticks")
	check(C.is_stall(sim.grid.tile_at(v.cell)), "parked on a stall tile")
	check(sim.grid.tile_at(v.cell) == C.Tile.STALL_STANDARD, "Standard car uses a standard stall")
	check(sim.grid.stall_owner[sim.grid.idx(v.cell)] == v.id, "stall reserved by this vehicle")
	check(v.depart_tick - v.park_start >= 400 and v.depart_tick - v.park_start <= 4800,
		"park duration within VehicleTypes.json bounds")

	var stall: Vector2i = v.cell
	var balance_before := sim.balance
	v.depart_tick = sim.tick_count + 1
	ticks = run_until(sim, func(): return not sim.vehicles.has(v.id), 600)
	check(ticks >= 0, "vehicle leaves within 600 ticks of departure")
	check(sim.stats["served"] == 1, "counted as served")
	check(sim.grid.stall_owner[sim.grid.idx(stall)] == 0, "stall freed after exit")
	var fee := sim.balance - balance_before
	check(fee >= C.HOURLY_RATE * C.MIN_BILLED_HOURS - 0.001, "paid at least the minimum fee (paid $%.2f)" % fee)
	var lane_total := 0
	for n in sim.grid.vehicle_count:
		lane_total += n
	check(lane_total == 0, "occupancy index empty after the only car left")
	return true


func test_stall_compatibility() -> bool:
	for type_name in C.VEHICLE_TYPES:
		var sim := quiet_sim()
		var v = sim.spawn_vehicle(type_name)
		run_until(sim, func(): return v.is_parked(), 800)
		var allowed: Array = data.vehicle_defs[type_name]["stalls"]
		check(v.is_parked(), "%s parks" % type_name)
		check(allowed.has(sim.grid.tile_at(v.cell)), "%s parked in an allowed stall type" % type_name)
	return true


func test_decision_branch_applies_json_deltas() -> bool:
	var sim := quiet_sim()
	var inc = sim.spawn_incident("SuspiciousVehicle")
	check(inc != null, "incident spawned")
	check(inc.state == C.IncidentState.DECISION_PENDING, "SuspiciousVehicle needs a decision")
	check(inc.deadline_tick - sim.tick_count == 900, "45s timer = 900 ticks")
	check(sim.next_decision() == inc, "it is the next decision")

	check(sim.resolve_decision(inc.id, 0), "branch 0 accepted")
	check(approx(sim.factions["police"], 5.0), "police +5 (got %.1f)" % sim.factions["police"])
	check(approx(sim.factions["shadow_clients"], -10.0), "shadow -10 (got %.1f)" % sim.factions["shadow_clients"])
	check(inc.state == C.IncidentState.RESOLVED, "incident resolved")
	check(sim.decision_log.size() == 1 and not sim.decision_log[0]["expired"], "decision logged")
	check(not sim.resolve_decision(inc.id, 1), "cannot decide the same incident twice")
	return true


func test_decision_expiry_fires_default_branch() -> bool:
	var sim := quiet_sim()
	var start_balance := sim.balance
	var inc = sim.spawn_incident("OverstayVehicle")   # default branch 1: Issue Fine Only
	run(sim, 1201)
	check(inc.state == C.IncidentState.EXPIRED, "expired after its 60s timer")
	check(approx(sim.factions["city_authority"], 1.0), "default branch applied city +1")
	check(approx(sim.balance - start_balance, 50.0), "fine_issued consequence paid $50 (got %.2f)" % (sim.balance - start_balance))
	check(sim.decision_log.size() == 1 and sim.decision_log[0]["expired"], "expiry logged as ignored")
	check(sim.stats["decisions_expired"] == 1, "expired decision counted")
	return true


func test_security_gated_branches() -> bool:
	var sim := quiet_sim()
	var inc = sim.spawn_incident("Altercation")   # 0: Security Handles It, 1: Call Police
	check(not sim.is_branch_available(inc, 0), "Security branch locked with no guard on staff")
	check(not sim.resolve_decision(inc.id, 0), "locked branch rejected")
	check(sim.is_branch_available(inc, 1), "non-security branch available")

	# Expiry with no guard falls back to the first available branch.
	run(sim, 701)
	check(inc.state == C.IncidentState.EXPIRED, "expired")
	check(approx(sim.factions["police"], 4.0), "fell back to 'Call Police' (police +4)")

	sim.hire_staff(C.Role.SECURITY)
	var inc2 = sim.spawn_incident("Altercation")
	check(sim.is_branch_available(inc2, 0), "Security branch unlocked after hiring a guard")
	check(sim.resolve_decision(inc2.id, 0), "Security branch accepted")
	var responding := false
	for s in sim.staff.values():
		if s.role == C.Role.SECURITY and not s.is_idle():
			responding = true
	check(responding, "guard dispatched to the scene")
	return true


func test_staff_clears_blocking_incident() -> bool:
	var sim := quiet_sim()
	sim.hire_staff(C.Role.MAINTENANCE)
	var cell := Vector2i(12, 8)
	check(sim.grid.tile_at(cell) == C.Tile.LANE, "test cell is a lane")
	var inc = sim.spawn_incident("OilSpill", cell)
	check(sim.grid.is_blocked(cell), "oil spill blocks the lane")
	run(sim, 2)
	check(inc.state == C.IncidentState.IN_PROGRESS, "maintenance dispatched")
	var ticks := run_until(sim, func(): return inc.state == C.IncidentState.RESOLVED, 4000)
	check(ticks >= 0, "resolved within 4000 ticks")
	check(not sim.grid.is_blocked(cell), "lane reopened")
	check(sim.stats["resolved_by_staff"] == 1, "counted as staff-resolved")
	var worker = sim.staff.values()[0]
	check(worker.fatigue > 0.0, "working raised fatigue")
	return true


func test_unattended_incident_is_fined() -> bool:
	var sim := quiet_sim()
	var start_balance := sim.balance
	var inc = sim.spawn_incident("FenderBender")   # expire delta: city -1
	run(sim, C.AUTO_INCIDENT_TIMEOUT + 1)
	check(inc.state == C.IncidentState.EXPIRED, "unattended incident expired")
	check(approx(sim.factions["city_authority"], -1.0), "expiry delta city -1")
	check(approx(start_balance - sim.balance, C.AUTO_INCIDENT_EXPIRE_FINE), "fined $%d" % int(C.AUTO_INCIDENT_EXPIRE_FINE))
	check(not sim.grid.is_blocked(inc.cell), "tile reopened after expiry")
	return true


func test_tow_consequence_removes_vehicle() -> bool:
	var sim := quiet_sim()
	var v = sim.spawn_vehicle("Standard")
	run_until(sim, func(): return v.is_parked(), 600)
	var stall: Vector2i = v.cell
	var inc = sim.spawn_incident("OverstayVehicle")
	check(inc.vehicle_id == v.id and inc.cell == stall, "overstay incident attached to the parked car")
	var before := sim.balance
	check(sim.resolve_decision(inc.id, 0), "Tow branch accepted")
	sim.tick()
	check(not sim.vehicles.has(v.id), "car towed away")
	check(sim.grid.stall_owner[sim.grid.idx(stall)] == 0, "stall freed")
	check(approx(sim.balance - before, 75.0), "towing recovered $75")
	check(approx(sim.factions["shadow_clients"], -5.0), "shadow -5")
	return true


func test_day_end_wages_and_loan() -> bool:
	var sim := quiet_sim()
	sim.hire_staff(C.Role.ATTENDANT)     # 80
	sim.hire_staff(C.Role.SECURITY)      # 120
	sim.hire_staff(C.Role.MAINTENANCE)   # 100
	sim.take_loan()                      # +5000
	var before := sim.balance
	sim.tick_count = C.TICKS_PER_DAY - 1
	sim.tick()
	var expected_loan := C.LOAN_AMOUNT * (C.LOAN_INTEREST_RATE + C.LOAN_REPAYMENT_FRACTION)
	check(sim.day == 2, "day rolled over to 2")
	check(approx(before - sim.balance, 300.0 + expected_loan), "paid wages $300 + loan $%.0f (paid %.2f)" % [expected_loan, before - sim.balance])
	check(approx(sim.loan_principal, C.LOAN_AMOUNT * (1.0 - C.LOAN_REPAYMENT_FRACTION)), "principal reduced by 5%")
	check(sim.day_history.size() == 1, "day summary recorded")
	check(sim.stats["revenue"] == 0.0 and sim.stats["expenses"] == 0.0, "daily stats reset")
	return true


func test_bankruptcy() -> bool:
	var sim := quiet_sim()
	sim.balance = C.BANKRUPT_THRESHOLD - 1.0
	check(not sim.bankrupt, "not bankrupt before the day closes")
	sim.tick_count = C.TICKS_PER_DAY - 1
	sim.tick()
	check(sim.bankrupt, "bankrupt at day end below the threshold")
	return true


func test_city_events() -> bool:
	var sim := quiet_sim()
	sim.demand_scale = 1.0
	sim.factions["police"] = -40.0
	sim.tick_count = 10 * C.TICKS_PER_HOUR - 1   # 09:59 → 10:00 hour boundary
	sim.tick()
	check(sim.event_active("PoliceSweep"), "PoliceSweep starts when police <= -30")
	sim.tick()   # demand is recomputed at the start of the next tick
	var base: float = data.weekday_curve[10]
	check(approx(sim.current_lambda, base * 0.4, 0.001) or sim.active_events.size() > 1,
		"PoliceSweep cuts demand to ×0.4 (λ %.2f vs %.2f)" % [sim.current_lambda, base * 0.4])

	var sim2 := quiet_sim()
	sim2.day = 3
	sim2.tick_count = 2 * C.TICKS_PER_DAY + 19 * C.TICKS_PER_HOUR - 1
	sim2.tick()
	check(sim2.event_active("StadiumNight"), "StadiumNight starts at 19:00 on day 3")
	check(sim2.surge_active(), "StadiumNight turns on surge pricing")
	var v = sim2.spawn_vehicle("Standard")
	run_until(sim2, func(): return v.is_parked(), 600)
	v.depart_tick = sim2.tick_count + 1
	var before := sim2.balance
	run_until(sim2, func(): return not sim2.vehicles.has(v.id), 600)
	var hours := maxf(float(sim2.tick_count - 1 - v.park_start) / C.TICKS_PER_HOUR, C.MIN_BILLED_HOURS)
	check(sim2.balance - before >= C.HOURLY_RATE * hours * C.SURGE_MULTIPLIER * 0.95,
		"surge fee charged during StadiumNight")
	return true


func test_street_queue_overflow() -> bool:
	var sim := quiet_sim()
	sim.demand_scale = 400.0
	var max_queue := 0
	for _i in 600:
		sim.tick()
		max_queue = maxi(max_queue, sim.outside_queue.size())
	check(max_queue <= C.OUTSIDE_QUEUE_MAX, "street queue never exceeds %d" % C.OUTSIDE_QUEUE_MAX)
	check(sim.stats["lost_at_gate"] > 0, "overflow arrivals counted as lost at the gate")
	check(sim.stats["entered"] > 0, "cars still entered under overload")
	check(sim.grid.vehicles_on(sim.grid.entry_gate) <= C.MAX_VEHICLES_PER_TILE, "gate congestion cap respected")
	return true


func test_hud_and_main_scene_smoke() -> bool:
	# HUD against a busy sim — catches format-string and property errors.
	var Hud = load("res://scripts/game/hud.gd")
	var sim := quiet_sim(5)
	sim.demand_scale = 3.0
	sim.hire_staff(C.Role.MAINTENANCE)
	sim.spawn_incident("SuspiciousVehicle")
	sim.spawn_incident("Altercation")
	sim.spawn_incident("OilSpill")
	run(sim, 3000)
	var hud = Hud.new()
	root.add_child(hud)
	hud.sim = sim
	hud.speed_text = "Speed ×1"
	hud.demand_scale_text = "3"
	hud.refresh(1.0)
	hud.set_overlay_visible(true)
	hud.refresh(1.0)
	check(hud._top_label.text.begins_with("SUBLEVEL"), "top bar rendered")
	check(hud._left_info.text.contains("FACTIONS"), "left panel rendered")
	check(hud._overlay_text.text.contains("Day"), "summary overlay rendered")
	hud.queue_free()

	# Full main scene: _ready builds the session; drive a few frames by hand.
	var main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	check(main.sim != null and main.sim.staff.size() == 3, "main scene starts a session with the starter crew")

	# Auto-pause: a new decision pauses the game; answering the last one resumes it.
	check(main.auto_pause, "auto-pause is on by default")
	main.sim.spawn_incident("SuspiciousVehicle")
	check(main.paused, "a new decision card pauses the game")
	var paused_tick: int = main.sim.tick_count
	main._process(0.5)
	check(main.sim.tick_count == paused_tick, "no sim ticks while paused for a decision")
	main._choose_branch(1)   # Look the Other Way — never gated
	check(not main.paused, "answering the last pending decision resumes play")
	main.auto_pause = false  # keep the timing check below deterministic

	var t0: int = main.sim.tick_count
	for _i in 10:
		main._process(0.1)
	var advanced: int = main.sim.tick_count - t0
	check(advanced >= 19 and advanced <= 21, "10 frames of 0.1s at ×1 ≈ 20 sim ticks (got %d)" % advanced)
	main._step_speed(1)
	check(main.hud.speed_text == "Speed ×2", "speed control updates HUD")
	main._set_demand(2.0)
	check(main.sim.demand_scale == 2.0, "demand control reaches the sim")
	var hire_before: int = main.sim.staff.size()
	main._hire(C.Role.SECURITY)
	check(main.sim.staff.size() == hire_before + 1, "hire action works")
	main._fire(C.Role.SECURITY)
	check(main.sim.staff.size() == hire_before, "fire action works")
	main.logger.write(main.sim, "test")
	check(FileAccess.file_exists(main.logger.path), "playtest log file written")
	main.queue_free()
	return true


func test_soak_three_days() -> bool:
	var sim := Simulation.new()
	sim.setup(99, data)
	sim.demand_scale = 3.0
	sim.hire_staff(C.Role.ATTENDANT)
	sim.hire_staff(C.Role.SECURITY)
	sim.hire_staff(C.Role.MAINTENANCE)
	sim.hire_staff(C.Role.MAINTENANCE)

	var total_ticks := 3 * C.TICKS_PER_DAY
	var invariant_failures := 0
	var max_stuck := 0
	var t0 := Time.get_ticks_usec()
	for i in total_ticks:
		sim.tick()
		# Decide pending cards like a hasty player: first available branch.
		if i % 100 == 0:
			var inc = sim.next_decision()
			if inc != null:
				for b in inc.def["branches"].size():
					if sim.resolve_decision(inc.id, b):
						break
		if i % 600 == 0:
			var problems := _check_invariants(sim)
			if not problems.is_empty():
				invariant_failures += 1
				if invariant_failures <= 3:
					print("    invariant problems at tick %d: %s" % [sim.tick_count, ", ".join(PackedStringArray(problems))])
			for v in sim.vehicles.values():
				if not v.is_parked():
					max_stuck = maxi(max_stuck, sim.tick_count - v.last_move_tick)
	var usec_per_tick := float(Time.get_ticks_usec() - t0) / total_ticks

	check(invariant_failures == 0, "occupancy/reservation/blocking invariants hold (%d bad samples)" % invariant_failures)
	check(sim.day_history.size() == 3, "three days completed (got %d)" % sim.day_history.size())
	check(max_stuck < 3000, "no moving car stuck longer than 150s (worst %d ticks)" % max_stuck)
	var served := 0
	for d in sim.day_history:
		served += int(d["served"])
	check(served > 50, "cars were served across 3 days (got %d)" % served)
	check(is_finite(sim.balance), "balance is a finite number")
	check(usec_per_tick < 5000.0, "sim fast enough for ×16 (%.0f µs/tick)" % usec_per_tick)

	print("    soak: %.0f µs/tick, served %d, balance $%.0f, factions %s" % [usec_per_tick, served, sim.balance, str(sim.factions)])
	for d in sim.day_history:
		var line := "    day %d: revenue $%.0f, expenses $%.0f, served %d, turned away %d, lost at gate %d, " \
			+ "peak %d/144, incidents %d (staff %d, unattended %d), decisions %d/%d"
		print(line % [
			d["day"], d["revenue"], d["expenses"], d["served"], d["turned_away"], d["lost_at_gate"],
			d["peak_occupancy"], d["incidents"], d["resolved_by_staff"], d["incidents_unattended"],
			d["decisions_made"], d["decisions_made"] + d["decisions_expired"]])
		print("      revenue by source %s · expenses by %s · events %s" % [
			str(d["revenue_by"]), str(d["expenses_by"]), str(d["city_events"])])
	return true


## Cross-checks the grid's bookkeeping against the agents. Returns problem descriptions.
func _check_invariants(sim: Simulation) -> Array[String]:
	var problems: Array[String] = []
	var grid = sim.grid

	# 1. Lane/gate vehicle counts match the vehicles standing there.
	var expected := {}
	for v in sim.vehicles.values():
		if v.state != C.VehicleState.DONE and not C.is_stall(grid.tile_at(v.cell)):
			expected[v.cell] = int(expected.get(v.cell, 0)) + 1
	for y in grid.height:
		for x in grid.width:
			var cell := Vector2i(x, y)
			if C.is_stall(grid.tile_at(cell)):
				continue
			if grid.vehicles_on(cell) != int(expected.get(cell, 0)):
				problems.append("count %s: grid %d vs %d" % [cell, grid.vehicles_on(cell), int(expected.get(cell, 0))])

	# 2. Every reserved stall belongs to a live vehicle that holds it.
	for s in grid.stalls:
		var owner_id: int = grid.stall_owner[grid.idx(s)]
		if owner_id == 0:
			continue
		var v = sim.vehicles.get(owner_id)
		if v == null:
			problems.append("stall %s owned by missing vehicle %d" % [s, owner_id])
		elif v.stall != s:
			problems.append("stall %s owned by vehicle %d which holds %s" % [s, owner_id, v.stall])

	# 3. Every vehicle's stall is reserved to it.
	for v in sim.vehicles.values():
		if v.stall != C.NO_CELL and grid.stall_owner[grid.idx(v.stall)] != v.id:
			problems.append("vehicle %d thinks it holds %s" % [v.id, v.stall])

	# 4. Blocked tiles are exactly the open blocking incidents.
	var blocking_cells := {}
	for inc in sim.incidents.values():
		if inc.is_open() and inc.blocking:
			blocking_cells[inc.cell] = true
	for y in grid.height:
		for x in grid.width:
			var cell := Vector2i(x, y)
			if grid.is_blocked(cell) != blocking_cells.has(cell):
				problems.append("blocked flag mismatch at %s" % cell)
	return problems
