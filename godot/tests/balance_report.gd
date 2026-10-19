extends SceneTree
## Headless balance report: simulates sessions for two player policies at
## several demand levels, prints per-day averages, and checks them against
## the tension targets below. Use it after every tuning change.
##
##   godot --headless --path godot --script res://tests/balance_report.gd
##   godot --headless --path godot --script res://tests/balance_report.gd -- --days=7 --seeds=5
##
## Day 1 (06:00-24:00) is a warm-up and is excluded from the averages.
## Policies (both keep the starter crew of 1 attendant, 1 guard, 1 maintenance):
##   attentive  — answers every decision after ~10s, picking the branch with
##                the best money + faction outcome; never takes loans
##   neglectful — never answers a decision card
## Results are also written to user://balance_report.json.

const C = preload("res://scripts/sim/sim_const.gd")
const Simulation = preload("res://scripts/sim/simulation.gd")
const DataLoader = preload("res://scripts/sim/data_loader.gd")

const POLICIES := ["attentive", "neglectful"]

# Tension targets at demand ×1 (the data as shipped).
const TARGET_PROFIT_MIN := 50.0       # attentive: profitable, but only just
const TARGET_PROFIT_MAX := 400.0
const TARGET_PEAK_MIN := 0.85         # attentive: lot ≥ 85% full at the busiest moment
const TARGET_OVERFLOW_MIN := 1.0      # attentive: some cars turned away / lost per day
const TARGET_OVERFLOW_MAX := 15.0     #            …but not a flood


func _initialize() -> void:
	var days := 5
	var seeds := 3
	var demands: Array[float] = [0.5, 1.0, 1.5, 2.0, 3.0]
	for arg in OS.get_cmdline_user_args():
		var a := String(arg)
		if a.begins_with("--days="):
			days = maxi(1, int(a.substr(7)))
		elif a.begins_with("--seeds="):
			seeds = maxi(1, int(a.substr(8)))
		elif a.begins_with("--demands="):
			demands.clear()
			for part in a.substr(10).split(","):
				demands.append(float(part))

	var data := DataLoader.new()
	if not data.load_all():
		push_error("Data load failed: %s" % str(data.errors))
		quit(1)
		return

	var probe := Simulation.new()
	probe.setup(1, data)
	var stalls: int = probe.grid.stalls.size()

	print("\n=== SUBLEVEL balance report — %d full day(s) × %d seed(s) per row ===" % [days, seeds])
	print("Lot: %d stalls. Start: $%d cash. Daily fixed costs: rent $%d + starter wages $%d. Bankrupt below $%d." % [
		stalls, int(C.STARTING_BALANCE), int(C.DAILY_RENT),
		int(C.STAFF_WAGES[0] + C.STAFF_WAGES[1] + C.STAFF_WAGES[2]), int(C.BANKRUPT_THRESHOLD)])

	var report := {}
	for policy in POLICIES:
		print("\n--- %s player ---" % policy)
		print("%-6s %8s %8s %8s %6s %6s %5s %5s %5s %5s %5s %6s %9s" % [
			"demand", "profit", "revenue", "costs", "served", "turned", "lost",
			"peak%", "avg%", "rep", "incid", "ignord", "bankrupt"])
		var rows: Array = []
		for demand in demands:
			var row := _run_row(data, policy, demand, days, seeds, stalls)
			rows.append(row)
			print("%-6s %8.0f %8.0f %8.0f %6.1f %6.1f %5.1f %5.0f %5.0f %5.0f %5.1f %6.1f %9s" % [
				"x%s" % str(demand), row["profit"], row["revenue"], row["expenses"],
				row["served"], row["turned_away"], row["lost_at_gate"],
				row["peak_pct"], row["avg_pct"], row["reputation"], row["incidents"],
				row["decisions_expired"], "%d/%d" % [row["bankrupt_runs"], seeds]])
		report[policy] = rows

	print("\nColumns (per full day): served/turned/lost = cars (turned = no stall in time, lost = gave up in the")
	print("street queue), peak%/avg% = stall occupancy, rep = reputation at the end, incid = incidents,")
	print("ignord = decision cards that timed out, bankrupt = runs that went bankrupt.")

	_print_targets(report, demands)

	var f := FileAccess.open("user://balance_report.json", FileAccess.WRITE)
	if f != null:
		f.store_string(JSON.stringify({"days": days, "seeds": seeds, "stalls": stalls, "rows": report}, "  "))
		f.close()
		print("\nSaved: %s" % ProjectSettings.globalize_path("user://balance_report.json"))
	quit(0)


func _run_row(data: DataLoader, policy: String, demand: float, days: int, seeds: int, stalls: int) -> Dictionary:
	var keys := ["revenue", "expenses", "served", "turned_away", "lost_at_gate",
		"peak_occupancy", "avg_occupancy", "incidents", "decisions_expired"]
	var totals := {}
	for k in keys:
		totals[k] = 0.0
	var day_count := 0
	var rep_total := 0.0
	var bankrupt_runs := 0
	for s in seeds:
		var sim := _run_session(data, policy, 1000 + s, demand, days)
		rep_total += sim.reputation
		if sim.bankrupt:
			bankrupt_runs += 1
		for d in sim.day_history.slice(1):   # skip the partial warm-up day
			day_count += 1
			for k in keys:
				totals[k] = float(totals[k]) + float(d[k])
	var n := float(maxi(day_count, 1))
	var row := {"demand": demand, "bankrupt_runs": bankrupt_runs, "reputation": rep_total / seeds}
	for k in keys:
		row[k] = float(totals[k]) / n
	row["profit"] = float(row["revenue"]) - float(row["expenses"])
	row["peak_pct"] = 100.0 * float(row["peak_occupancy"]) / stalls
	row["avg_pct"] = 100.0 * float(row["avg_occupancy"]) / stalls
	return row


func _run_session(data: DataLoader, policy: String, session_seed: int, demand: float, days: int) -> Simulation:
	var sim := Simulation.new()
	sim.setup(session_seed, data)
	sim.demand_scale = demand
	sim.hire_staff(C.Role.ATTENDANT)
	sim.hire_staff(C.Role.SECURITY)
	sim.hire_staff(C.Role.MAINTENANCE)
	# Sessions start at 06:00, so day 1 is partial: run it as warm-up and
	# then simulate `days` full 24h days (only those are averaged).
	var first_midnight := sim.tick_count - sim.tick_count % C.TICKS_PER_DAY + C.TICKS_PER_DAY
	var end_tick := first_midnight + days * C.TICKS_PER_DAY
	while sim.tick_count < end_tick and not sim.bankrupt:
		sim.tick()
		if policy == "attentive":
			var inc = sim.next_decision()
			if inc != null and sim.tick_count - inc.spawn_tick >= 200:
				sim.resolve_decision(inc.id, _best_branch(sim, inc))
	return sim


## Branch with the best money + faction outcome among those available.
func _best_branch(sim: Simulation, inc) -> int:
	var best := -1
	var best_score := -INF
	var branches: Array = inc.def["branches"]
	for i in branches.size():
		if not sim.is_branch_available(inc, i):
			continue
		var br: Dictionary = branches[i]
		var score := float(C.CONSEQUENCE_MONEY.get(String(br["consequence"]), 0.0))
		for f in br["delta"].keys():
			score += 2.0 * float(br["delta"][f])
		if score > best_score:
			best_score = score
			best = i
	return best


func _row_for(rows: Array, demand: float) -> Dictionary:
	for r in rows:
		if absf(float(r["demand"]) - demand) < 0.001:
			return r
	return {}


func _print_targets(report: Dictionary, demands: Array[float]) -> void:
	if not demands.has(1.0):
		return
	var att := _row_for(report["attentive"], 1.0)
	var neg := _row_for(report["neglectful"], 1.0)
	var overflow := float(att["turned_away"]) + float(att["lost_at_gate"])
	print("\n=== Tension targets at demand ×1 ===")
	_target("attentive profit $%.0f/day within $%.0f-$%.0f" % [att["profit"], TARGET_PROFIT_MIN, TARGET_PROFIT_MAX],
		att["profit"] >= TARGET_PROFIT_MIN and att["profit"] <= TARGET_PROFIT_MAX)
	_target("attentive peak occupancy %.0f%% ≥ %.0f%%" % [att["peak_pct"], TARGET_PEAK_MIN * 100.0],
		att["peak_pct"] >= TARGET_PEAK_MIN * 100.0)
	_target("attentive overflow %.1f cars/day within %.0f-%.0f" % [overflow, TARGET_OVERFLOW_MIN, TARGET_OVERFLOW_MAX],
		overflow >= TARGET_OVERFLOW_MIN and overflow <= TARGET_OVERFLOW_MAX)
	_target("neglectful player loses money ($%.0f/day) or goes bankrupt (%d runs)" % [neg["profit"], neg["bankrupt_runs"]],
		neg["profit"] < 0.0 or int(neg["bankrupt_runs"]) > 0)


func _target(label: String, ok: bool) -> void:
	print("  [%s] %s" % ["HIT " if ok else "MISS", label])
