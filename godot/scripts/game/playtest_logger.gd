extends RefCounted
## Writes one JSON file per playtest session to user://playtests/.
## Rewritten at every day end, on restart and on quit, so a crash or a
## closed window still leaves the data up to the last finished day.
##
## Windows: %APPDATA%\Godot\app_userdata\SUBLEVEL Playtest\playtests\
## Linux:   ~/.local/share/godot/app_userdata/SUBLEVEL Playtest/playtests/

const C = preload("res://scripts/sim/sim_const.gd")

var path := ""
var session: Dictionary = {}
var _real_start_msec := 0


func start(sim, settings: Dictionary) -> void:
	var stamp := Time.get_datetime_string_from_system(false, true).replace(":", "-").replace(" ", "_")
	DirAccess.make_dir_recursive_absolute("user://playtests")
	path = "user://playtests/session_%s_seed%d.json" % [stamp, sim.seed_value]
	_real_start_msec = Time.get_ticks_msec()
	session = {
		"build": "sublevel-godot-playtest 0.1",
		"started": Time.get_datetime_string_from_system(false, true),
		"seed": sim.seed_value,
		"settings": settings,
		"actions": [],
	}


## Records a player action (hire, fire, loan, speed/demand change...).
func note(sim, action: String, detail: Variant = null) -> void:
	session["actions"].append({
		"day": sim.day, "clock": sim.clock_text(), "action": action,
		"detail": detail, "real_seconds": real_seconds(),
	})


func real_seconds() -> float:
	return (Time.get_ticks_msec() - _real_start_msec) / 1000.0


func write(sim, reason: String) -> void:
	if path == "":
		return
	session["written_because"] = reason
	session["real_minutes_played"] = snappedf(real_seconds() / 60.0, 0.01)
	session["days"] = sim.day_history
	session["today_partial"] = sim.stats
	session["decisions"] = sim.decision_log
	session["final"] = {
		"day": sim.day, "clock": sim.clock_text(),
		"balance": sim.balance, "loan_principal": sim.loan_principal,
		"factions": sim.factions, "integrity": sim.grid.integrity,
		"bankrupt": sim.bankrupt, "goal_reached": sim.goal_reached,
		"staff": sim.staff.size(), "demand_scale": sim.demand_scale,
	}
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		push_warning("Playtest log not written: %s" % error_string(FileAccess.get_open_error()))
		return
	f.store_string(JSON.stringify(session, "  "))
	f.close()


func absolute_path() -> String:
	return ProjectSettings.globalize_path(path)
