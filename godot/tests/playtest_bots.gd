extends SceneTree
## Plays full 7-day sessions with scripted "players" of different styles
## and reports what a playtester would feel: do choices matter, is there a
## dominant strategy, how often does something happen, does it get harder,
## can you lose. Read-only analysis — it doesn't change the game.
##
##   godot --headless --path godot --script res://tests/playtest_bots.gd

const C = preload("res://scripts/sim/sim_const.gd")
const Simulation = preload("res://scripts/sim/simulation.gd")
const DataLoader = preload("res://scripts/sim/data_loader.gd")

const DAYS := 7
const SEEDS := [11, 22, 33, 44]
const REACTION_TICKS := 200          # a player answers a card ~10 s after it appears
const POLICIES := {
	"careful": "answers every card (best money + faction score), keeps starter crew",
	"smart_manager": "careful + hires/fires each hour based on what's going wrong",
	"greedy": "always takes the most money, fires Attendant + Security to cut wages",
	"shady": "always sides with Shadow clients",
	"lawful": "always sides with Police + City",
	"loan_shark": "careful, but takes the full $10k of loans on day 1",
	"absent": "never answers a card, never manages anything",
}

var data: DataLoader


func _initialize() -> void:
	data = DataLoader.new()
	data.load_all()
	print("\n=== SUBLEVEL playtest bots — %d days × %d seeds per player style ===" % [DAYS, SEEDS.size()])
	var results := {}
	for policy in POLICIES.keys():
		var runs: Array = []
		for s in SEEDS:
			runs.append(_play(policy, s))
		results[policy] = runs
	_report_outcomes(results)
	_report_pacing(results["careful"])
	_report_progression(results["careful"])
	_report_decisions(results)
	quit(0)


# ── One session ──────────────────────────────────────────────────

func _play(policy: String, session_seed: int) -> Dictionary:
	var sim := Simulation.new()
	sim.setup(session_seed, data)
	sim.hire_staff(C.Role.ATTENDANT)
	sim.hire_staff(C.Role.SECURITY)
	sim.hire_staff(C.Role.MAINTENANCE)
	if policy == "greedy":
		sim.fire_staff(C.Role.ATTENDANT)
		sim.fire_staff(C.Role.SECURITY)
	if policy == "loan_shark":
		sim.take_loan()
		sim.take_loan()

	var moments := {"decision": [], "incident": [], "event": []}
	var choices: Dictionary = {}   # "Type → branch" -> count
	sim.decision_required.connect(func(_inc): moments["decision"].append(sim.tick_count))
	var last_incident_id := sim._next_incident_id
	var last_event_count := 0
	var min_balance := sim.balance
	var hires := 0
	var fires := 0
	var end_tick := DAYS * C.TICKS_PER_DAY

	while sim.tick_count < end_tick and not sim.bankrupt:
		sim.tick()
		if sim._next_incident_id != last_incident_id:
			last_incident_id = sim._next_incident_id
			moments["incident"].append(sim.tick_count)
		if sim.active_events.size() > last_event_count:
			moments["event"].append(sim.tick_count)
		last_event_count = sim.active_events.size()
		min_balance = minf(min_balance, sim.balance)

		if policy != "absent":
			var inc = sim.next_decision()
			if inc != null and sim.tick_count - inc.spawn_tick >= REACTION_TICKS:
				var b := _choose(policy, sim, inc)
				if b >= 0:
					var key := "%s → %s" % [inc.type_name, inc.def["branches"][b]["label"]]
					choices[key] = int(choices.get(key, 0)) + 1
					sim.resolve_decision(inc.id, b)
		if policy == "smart_manager" and sim.tick_count % C.TICKS_PER_HOUR == 0:
			var acted := _manage(sim)
			hires += acted.x
			fires += acted.y

	return {
		"policy": policy, "seed": session_seed, "sim": sim,
		"bankrupt": sim.bankrupt, "bankrupt_day": sim.day if sim.bankrupt else 0,
		"final_balance": sim.balance, "min_balance": min_balance,
		"net_worth": sim.balance - sim.loan_principal,
		"reputation": sim.reputation, "factions": sim.factions.duplicate(),
		"days": sim.day_history.duplicate(true), "moments": moments,
		"choices": choices, "hires": hires, "fires": fires,
		"staff": sim.staff.size(),
	}


func _choose(policy: String, sim: Simulation, inc) -> int:
	var best := -1
	var best_score := -INF
	var branches: Array = inc.def["branches"]
	for i in branches.size():
		if not sim.is_branch_available(inc, i):
			continue
		var br: Dictionary = branches[i]
		var money := float(C.CONSEQUENCE_MONEY.get(String(br["consequence"]), 0.0))
		var d: Dictionary = br["delta"]
		var score := 0.0
		match policy:
			"greedy":
				score = money
			"shady":
				score = float(d.get("shadow_clients", 0.0)) * 10.0 + money * 0.01
			"lawful":
				score = (float(d.get("police", 0.0)) + float(d.get("city_authority", 0.0))) * 10.0 + money * 0.01
			_:
				score = money
				for f in d.keys():
					score += 2.0 * float(d[f])
		if score > best_score:
			best_score = score
			best = i
	return best


## Hourly management for the smart_manager. Returns Vector2i(hires, fires).
func _manage(sim: Simulation) -> Vector2i:
	var hires := 0
	var fires := 0
	var waiting := 0
	for inc in sim.incidents.values():
		if inc.state == C.IncidentState.ACTIVE and inc.staff_id == 0 and sim.tick_count - inc.spawn_tick > 300:
			waiting += 1
	if waiting > 0 and sim.count_staff(C.Role.MAINTENANCE) < 3 and sim.balance > 800:
		if sim.hire_staff(C.Role.MAINTENANCE) != null:
			hires += 1
	var overflow := int(sim.stats["turned_away"]) + int(sim.stats["lost_at_gate"])
	if overflow > 6 and sim.count_staff(C.Role.ATTENDANT) < 2 and sim.balance > 800:
		if sim.hire_staff(C.Role.ATTENDANT) != null:
			hires += 1
	if sim.balance < 300 and sim.count_staff(C.Role.MAINTENANCE) > 1:
		if sim.fire_staff(C.Role.MAINTENANCE):
			fires += 1
	return Vector2i(hires, fires)


# ── Reports ──────────────────────────────────────────────────────

func _avg(values: Array) -> float:
	if values.is_empty():
		return 0.0
	var t := 0.0
	for v in values:
		t += float(v)
	return t / values.size()


func _report_outcomes(results: Dictionary) -> void:
	print("\n--- Outcomes after %d days (average of %d seeds) ---" % [DAYS, SEEDS.size()])
	print("%-14s %9s %9s %9s %6s %8s %7s %7s %7s  %s" % [
		"player", "balance", "networth", "lowest", "rep", "bankrupt", "city", "police", "shadow", "style"])
	for policy in results.keys():
		var runs: Array = results[policy]
		var bal: Array = []
		var nw: Array = []
		var low: Array = []
		var rep: Array = []
		var fc := {"city_authority": [], "police": [], "shadow_clients": []}
		var bankrupt := 0
		for r in runs:
			bal.append(r["final_balance"])
			nw.append(r["net_worth"])
			low.append(r["min_balance"])
			rep.append(r["reputation"])
			for f in fc.keys():
				fc[f].append(r["factions"][f])
			if r["bankrupt"]:
				bankrupt += 1
		print("%-14s %9.0f %9.0f %9.0f %6.0f %8s %7.0f %7.0f %7.0f  %s" % [
			policy, _avg(bal), _avg(nw), _avg(low), _avg(rep), "%d/%d" % [bankrupt, runs.size()],
			_avg(fc["city_authority"]), _avg(fc["police"]), _avg(fc["shadow_clients"]), POLICIES[policy]])

	# City events each style triggered.
	print("\n--- City events seen per run (all styles) ---")
	for policy in results.keys():
		var counts := {}
		for r in results[policy]:
			for d in r["days"]:
				for e in d["city_events"]:
					counts[e] = int(counts.get(e, 0)) + 1
		var parts: Array[String] = []
		for k in counts.keys():
			parts.append("%s %.1f" % [k, float(counts[k]) / results[policy].size()])
		print("%-14s %s" % [policy, ", ".join(PackedStringArray(parts)) if not parts.is_empty() else "none"])


func _report_pacing(runs: Array) -> void:
	print("\n--- Pacing for the careful player (real time at ×1 speed; one day = 12 min) ---")
	var per_day_dec: Array = []
	var per_day_inc: Array = []
	var gaps_all: Array = []
	var dec_gaps: Array = []
	var quiet_share: Array = []
	for r in runs:
		var m: Dictionary = r["moments"]
		var days_played := float(DAYS) - float(C.START_HOUR) / 24.0
		per_day_dec.append(m["decision"].size() / days_played)
		per_day_inc.append(m["incident"].size() / days_played)
		var everything: Array = m["decision"] + m["incident"] + m["event"]
		everything.sort()
		var quiet_ticks := 0
		for i in range(1, everything.size()):
			var g: int = everything[i] - everything[i - 1]
			gaps_all.append(g * C.TICK_SECONDS)
			if g * C.TICK_SECONDS >= 60.0:
				quiet_ticks += g
		var total_ticks := float(DAYS * C.TICKS_PER_DAY - C.START_HOUR * C.TICKS_PER_HOUR)
		quiet_share.append(100.0 * quiet_ticks / total_ticks)
		var dec: Array = m["decision"].duplicate()
		dec.sort()
		for i in range(1, dec.size()):
			dec_gaps.append((dec[i] - dec[i - 1]) * C.TICK_SECONDS)
	gaps_all.sort()
	dec_gaps.sort()
	print("Decision cards per in-game day: %.1f   incidents per day: %.1f" % [_avg(per_day_dec), _avg(per_day_inc)])
	if not gaps_all.is_empty():
		print("Time between ANY new happening (card / incident / city event): median %.0fs, 90th pct %.0fs, longest %.0fs" % [
			gaps_all[gaps_all.size() / 2], gaps_all[int(gaps_all.size() * 0.9)], gaps_all.back()])
	if not dec_gaps.is_empty():
		print("Time between decision cards: median %.0fs, 90th pct %.0fs, longest %.0fs" % [
			dec_gaps[dec_gaps.size() / 2], dec_gaps[int(dec_gaps.size() * 0.9)], dec_gaps.back()])
	print("Share of play time spent in quiet stretches of 60s+ with nothing new: %.0f%%" % _avg(quiet_share))


func _report_progression(runs: Array) -> void:
	print("\n--- Day-by-day for the careful player (average of seeds) ---")
	print("%-5s %8s %8s %8s %6s %6s %6s %6s" % ["day", "revenue", "costs", "profit", "rep", "served", "turned", "cards"])
	for i in DAYS:
		var rev: Array = []
		var cost: Array = []
		var rep: Array = []
		var served: Array = []
		var turned: Array = []
		var cards: Array = []
		for r in runs:
			if i < r["days"].size():
				var d: Dictionary = r["days"][i]
				rev.append(d["revenue"])
				cost.append(d["expenses"])
				rep.append(d["reputation"])
				served.append(d["served"])
				turned.append(int(d["turned_away"]) + int(d["lost_at_gate"]))
				cards.append(int(d["decisions_made"]) + int(d["decisions_expired"]))
		print("%-5d %8.0f %8.0f %8.0f %6.0f %6.0f %6.0f %6.0f" % [
			i + 1, _avg(rev), _avg(cost), _avg(rev) - _avg(cost), _avg(rep), _avg(served), _avg(turned), _avg(cards)])


func _report_decisions(results: Dictionary) -> void:
	print("\n--- What the careful player picked (all seeds) ---")
	var totals := {}
	for r in results["careful"]:
		for k in r["choices"].keys():
			totals[k] = int(totals.get(k, 0)) + int(r["choices"][k])
	var keys: Array = totals.keys()
	keys.sort_custom(func(a, b): return totals[a] > totals[b])
	for k in keys:
		print("  %4d  %s" % [totals[k], k])
	var mgr: Array = results["smart_manager"]
	print("\nsmart_manager: %.1f hires, %.1f fires per run; ends with %.1f staff" % [
		_avg(mgr.map(func(r): return r["hires"])), _avg(mgr.map(func(r): return r["fires"])),
		_avg(mgr.map(func(r): return r["staff"]))])
