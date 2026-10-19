extends RefCounted
## Guided tutorial on a tiny 12-stall lot. A list of steps, each with:
##   title / text   shown in the HUD tutorial panel
##   setup()        runs when the step starts (spawn cars, incidents…)
##   done()         true once the player has done the thing
##   highlight()    tiles to pulse on the map
##   status()       optional live line (countdowns)
##   auto()         performs the step's action the way a player would —
##                  used by tests and by --tutorial-skip-to
##   manual         true → the step waits for the Next button instead of done()
## `main` is the main.gd node (untyped to avoid a preload cycle).

const C = preload("res://scripts/sim/sim_const.gd")
const GameConfig = preload("res://scripts/game/game_config.gd")

const OIL_SPILL_CELL := Vector2i(6, 5)   # lane in front of the stalls on the 12x8 lot
const RUSH_TICKS := 1200                 # one in-game hour… ×2 (60 sim seconds)
const DONE_DISPLAY_SECONDS := 1.2        # how long the ✓ shows before moving on

var main
var steps: Array = []
var step_index := 0
var done_timer := -1.0                   # ≥ 0 while showing "✓ done" for this step

var _step_start_tick := 0
var _target_vehicle_id := 0
var _served_baseline := 0
var _incident_id := 0
var _decisions_baseline := 0
var _maintenance_baseline := 0
var _rush_end_tick := 0


func _init(game) -> void:
	main = game
	_build_steps()


func start() -> void:
	step_index = 0
	done_timer = -1.0
	_enter_step()


# ── Queries for the HUD and tests ────────────────────────────────

func current() -> Dictionary:
	return steps[step_index]


func step_count() -> int:
	return steps.size()


func is_finished() -> bool:
	return step_index == steps.size() - 1


func is_manual() -> bool:
	return bool(current().get("manual", false))


func status_text() -> String:
	var fn = current().get("status")
	return String(fn.call()) if fn != null else ""


func highlights() -> Array[Vector2i]:
	var out: Array[Vector2i] = []
	var fn = current().get("highlight")
	if fn != null:
		for cell in fn.call():
			out.append(cell)
	return out


# ── Flow ─────────────────────────────────────────────────────────

func update(delta: float) -> void:
	if is_finished():
		return
	if done_timer >= 0.0:
		done_timer -= delta
		if done_timer < 0.0:
			_advance()
		return
	var tick_fn = current().get("tick")
	if tick_fn != null:
		tick_fn.call()
	if not is_manual() and bool(current()["done"].call()):
		done_timer = DONE_DISPLAY_SECONDS


## Next button / Enter on manual steps.
func next() -> void:
	if is_manual() and not is_finished():
		_advance()


## Performs the current step's action like a player would.
func auto_complete_current() -> void:
	var fn = current().get("auto")
	if fn != null:
		fn.call()


## Debug/tests: play steps automatically until `target` (1-based) is reached.
func skip_to(target: int) -> void:
	var guard := 0
	while step_index < mini(target, steps.size()) - 1 and guard < 20000:
		var before := step_index
		auto_complete_current()
		var spins := 0
		while step_index == before and spins < 4000:
			main.sim.tick()
			update(DONE_DISPLAY_SECONDS + 0.1)
			spins += 1
		guard += 1


func _advance() -> void:
	var teardown = current().get("teardown")
	if teardown != null:
		teardown.call()
	step_index = mini(step_index + 1, steps.size() - 1)
	done_timer = -1.0
	_enter_step()


func _enter_step() -> void:
	_step_start_tick = main.sim.tick_count
	var setup = current().get("setup")
	if setup != null:
		setup.call()


# ── Helpers ──────────────────────────────────────────────────────

func _sim():
	return main.sim


func _total_served() -> int:
	var n := int(_sim().stats["served"])
	for d in _sim().day_history:
		n += int(d["served"])
	return n


func _decisions_made() -> int:
	var n := 0
	for d in _sim().decision_log:
		if not d["expired"]:
			n += 1
	return n


func _incident_cell() -> Array:
	var inc = _sim().incidents.get(_incident_id)
	return [inc.cell] if inc != null else []


# ── Steps ────────────────────────────────────────────────────────
# Each step's behaviour lives in named methods (no inline lambdas), which
# keeps the step table readable and parser-safe.

# gdlint: disable=max-line-length
func _build_steps() -> void:
	steps = [
		{
			"title": "Welcome to your garage",
			"text": "This is a small 12-stall garage. Cars come in at the green IN gate, look for a free stall that fits, and pay when they leave through the red OUT gate.\nThe clock is stopped. Press Next (or Enter) to continue.",
			"manual": true,
			"setup": _welcome_setup,
			"highlight": _gates,
			"auto": next,
		},
		{
			"title": "Time controls",
			"text": "Press + (or the Faster button) to start the clock at double speed.\nSpace pauses and resumes, − slows down. You can go up to ×16.",
			"done": _time_done,
			"auto": _time_auto,
		},
		{
			"title": "Customers arrive",
			"text": "Three customers are pulling in. Watch each one find a stall that fits: compact cars can use the teal or blue stalls, SUVs need the big brown ones.\nHover over a stall to see who is parked there.",
			"setup": _arrive_setup,
			"done": _arrive_done,
			"highlight": _entry,
		},
		{
			"title": "Getting paid",
			"text": "Cars pay $3 for every in-game hour they stay, when they leave. One car is heading out now — watch Today's earnings in the top bar go up.",
			"setup": _paid_setup,
			"done": _paid_done,
			"highlight": _paid_highlight,
		},
		{
			"title": "Incidents",
			"text": "Oil spill! It blocks a lane (the pulsing ring), so cars have to drive around it. Your Maintenance worker (orange M) is on the way to clean it.\nIncidents nobody handles get you fined and hurt your reputation.",
			"setup": _spill_setup,
			"done": _incident_closed,
			"highlight": _incident_cell,
		},
		{
			"title": "Your first decision",
			"text": "Some problems need your call. The game pauses and a decision card appears at the bottom of the screen. Each option shows what it does to your faction standing and money.\nPick one with 1, 2 or 3 — or click it.",
			"setup": _decision_setup,
			"tick": _decision_tick,
			"done": _decision_done,
			"highlight": _incident_cell,
			"auto": _decision_auto,
		},
		{
			"title": "Hiring staff",
			"text": "One Maintenance worker can't be everywhere. Press F3 or click +Maint. (left panel) to hire a second one.\nStaff are paid every midnight: Attendant $80, Security $120, Maintenance $100.",
			"setup": _hire_setup,
			"done": _hire_done,
			"auto": _hire_auto,
		},
		{
			"title": "Rush hour",
			"text": "Rush hour! More cars than stalls. Drivers who can't find a stall leave, and that lowers your Reputation (left panel). Lower reputation means fewer customers tomorrow.\nYour Attendant keeps the gate moving. Ride it out — speed up if you like.",
			"setup": _rush_setup,
			"teardown": _rush_teardown,
			"done": _rush_done,
			"status": _rush_status,
			"highlight": _entry,
			"auto": _rush_auto,
		},
		{
			"title": "Your day at a glance",
			"text": "Every midnight you pay rent ($750 in a real game) plus wages, so profit is tight.\nPress Tab to open the summary: each day's revenue, costs and reputation, and every decision you made.",
			"done": _summary_done,
			"auto": _summary_auto,
		},
		{
			"title": "You're ready",
			"text": "That's the core loop: keep cars flowing, deal with problems, and watch your money and reputation.\nA real game gives you 40 stalls, $2,500 and 7 days to survive. Good luck!",
			"manual": true,
			"setup": _finish_setup,
		},
	]

# gdlint: enable=max-line-length

# 1 · Welcome
func _welcome_setup() -> void:
	main.paused = true
	main._update_speed_text()


func _gates() -> Array:
	return [_sim().grid.entry_gate, _sim().grid.exit_gate]


func _entry() -> Array:
	return [_sim().grid.entry_gate]


# 2 · Time controls
func _time_done() -> bool:
	return not main.paused and main.speed_index >= 1


func _time_auto() -> void:
	main._step_speed(1)


# 3 · Customers arrive
func _arrive_setup() -> void:
	for t in ["Compact", "Standard", "SUV"]:
		_sim().outside_queue.append({"type": t, "since": _sim().tick_count})


func _arrive_done() -> bool:
	return _sim().parked_vehicles().size() >= 3


# 4 · Getting paid
func _paid_setup() -> void:
	_served_baseline = _total_served()
	var parked: Array = _sim().parked_vehicles()
	if not parked.is_empty():
		_target_vehicle_id = parked[0].id
		parked[0].depart_tick = _sim().tick_count + 60


func _paid_done() -> bool:
	return _total_served() > _served_baseline


func _paid_highlight() -> Array:
	var v = _sim().vehicles.get(_target_vehicle_id)
	if v != null:
		return [v.cell, _sim().grid.exit_gate]
	return [_sim().grid.exit_gate]


# 5 · Incidents
func _spill_setup() -> void:
	var inc = _sim().spawn_incident("OilSpill", OIL_SPILL_CELL)
	_incident_id = inc.id if inc != null else 0


func _incident_closed() -> bool:
	var inc = _sim().incidents.get(_incident_id)
	return inc == null or not inc.is_open()


# 6 · Decisions
func _decision_setup() -> void:
	_decisions_baseline = _decisions_made()
	_spawn_decision()


func _decision_tick() -> void:
	# If the card timed out (e.g. the player unpaused), offer another.
	if _incident_closed() and _decisions_made() == _decisions_baseline:
		_spawn_decision()


func _decision_done() -> bool:
	return _decisions_made() > _decisions_baseline


func _decision_auto() -> void:
	var inc = _sim().next_decision()
	if inc != null:
		main._choose_branch(maxi(_sim().expiry_branch(inc), 0))


func _spawn_decision() -> void:
	var inc = _sim().spawn_incident("SuspiciousVehicle")
	_incident_id = inc.id if inc != null else 0


# 7 · Hiring
func _hire_setup() -> void:
	_maintenance_baseline = _sim().count_staff(C.Role.MAINTENANCE)


func _hire_done() -> bool:
	return _sim().count_staff(C.Role.MAINTENANCE) > _maintenance_baseline


func _hire_auto() -> void:
	main._hire(C.Role.MAINTENANCE)


# 8 · Rush hour
func _rush_setup() -> void:
	_rush_end_tick = _sim().tick_count + RUSH_TICKS
	_sim().demand_scale = 4.0


func _rush_teardown() -> void:
	_sim().demand_scale = 0.3


func _rush_done() -> bool:
	return _sim().tick_count >= _rush_end_tick


func _rush_status() -> String:
	var secs := maxi(0, int((_rush_end_tick - _sim().tick_count) * C.TICK_SECONDS))
	return "Rush ends in %ds of game time · reputation %d" % [secs, int(round(_sim().reputation))]


func _rush_auto() -> void:
	main._step_speed(2)


# 9 · Summary
func _summary_done() -> bool:
	return main.hud.overlay_visible


func _summary_auto() -> void:
	main.hud.toggle_overlay()


# 10 · Finish
func _finish_setup() -> void:
	main.hud.set_overlay_visible(false)
	main.paused = true
	main._update_speed_text()
	GameConfig.mark_tutorial_done()
