extends SceneTree
## Headless balance report: simulates several sessions per demand level
## with a fixed "reasonable player" policy and prints per-day averages.
## Use it to sanity-check Data/*.json tuning before a human playtest.
##
##   godot --headless --path godot --script res://tests/balance_report.gd
##   godot --headless --path godot --script res://tests/balance_report.gd -- --days=7 --seeds=5
##
## Policy: starter crew (1 attendant, 1 security, 1 maintenance), every
## decision answered after ~10s with the first allowed branch, no loans.
## Results are also written to user://balance_report.json.

const C = preload("res://scripts/sim/sim_const.gd")
const Simulation = preload("res://scripts/sim/simulation.gd")
const DataLoader = preload("res://scripts/sim/data_loader.gd")

const DEMAND_LEVELS := [0.5, 1.0, 2.0, 4.0]


func _initialize() -> void:
	var days := 3
	var seeds := 3
	for arg in OS.get_cmdline_user_args():
		var a := String(arg)
		if a.begins_with("--days="):
			days = maxi(1, int(a.substr(7)))
		elif a.begins_with("--seeds="):
			seeds = maxi(1, int(a.substr(8)))

	var data := DataLoader.new()
	if not data.load_all():
		push_error("Data load failed: %s" % str(data.errors))
		quit(1)
		return

	print("\n=== SUBLEVEL balance report — %d day(s) × %d seed(s) per demand level ===" % [days, seeds])
	print("Per-day averages. Stalls: 144. Starting balance: $%d.\n" % int(C.STARTING_BALANCE))
	print("%-7s %8s %8s %8s %6s %7s %6s %6s %6s %7s %7s %9s" % [
		"demand", "revenue", "expense", "profit", "served", "turned", "lost", "peak", "incid", "unattd", "ignored", "end bal"])

	var report: Array = []
	for demand in DEMAND_LEVELS:
		var totals := {}
		var end_balance := 0.0
		var bankrupt_runs := 0
		for s in seeds:
			var sim := _run_session(data, 1000 + s, demand, days)
			end_balance += sim.balance
			if sim.bankrupt:
				bankrupt_runs += 1
			for d in sim.day_history:
				for key in ["revenue", "expenses", "served", "turned_away", "lost_at_gate",
						"peak_occupancy", "incidents", "incidents_unattended", "decisions_expired"]:
					totals[key] = float(totals.get(key, 0.0)) + float(d[key])
		var n := float(days * seeds)
		var row := {"demand": demand, "bankrupt_runs": bankrupt_runs, "end_balance_avg": end_balance / seeds}
		for key in totals.keys():
			row[key] = totals[key] / n
		report.append(row)
		print("%-7s %8.0f %8.0f %8.0f %6.1f %7.1f %6.1f %6.1f %6.1f %7.1f %7.1f %9.0f%s" % [
			"x%s" % str(demand), row["revenue"], row["expenses"], row["revenue"] - row["expenses"],
			row["served"], row["turned_away"], row["lost_at_gate"], row["peak_occupancy"],
			row["incidents"], row["incidents_unattended"], row["decisions_expired"], row["end_balance_avg"],
			"  (%d bankrupt)" % bankrupt_runs if bankrupt_runs > 0 else ""])

	print("\nColumns: served/turned/lost = cars per day (turned = no stall in time, lost = gave up on the street queue)")
	print("peak = busiest stall occupancy, incid = incidents/day, unattd = auto incidents fined, ignored = decisions expired")

	var f := FileAccess.open("user://balance_report.json", FileAccess.WRITE)
	if f != null:
		f.store_string(JSON.stringify({"days": days, "seeds": seeds, "rows": report}, "  "))
		f.close()
		print("\nSaved: %s" % ProjectSettings.globalize_path("user://balance_report.json"))
	quit(0)


func _run_session(data: DataLoader, session_seed: int, demand: float, days: int) -> Simulation:
	var sim := Simulation.new()
	sim.setup(session_seed, data)
	sim.demand_scale = demand
	sim.hire_staff(C.Role.ATTENDANT)
	sim.hire_staff(C.Role.SECURITY)
	sim.hire_staff(C.Role.MAINTENANCE)
	var end_tick := sim.tick_count + days * C.TICKS_PER_DAY
	# Align to the day boundary so every simulated day is a full 24h day
	end_tick -= end_tick % C.TICKS_PER_DAY
	while sim.tick_count < end_tick:
		sim.tick()
		var inc = sim.next_decision()
		if inc != null and sim.tick_count - inc.spawn_tick >= 200:
			for b in inc.def["branches"].size():
				if sim.resolve_decision(inc.id, b):
					break
	return sim
