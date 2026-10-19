extends RefCounted
## Hand-off between the menu and the game scene, plus small persistent
## settings. Static members live on the script itself, so the menu can set
## `mode` and the game scene reads it after the scene change.

const SETTINGS_PATH := "user://sublevel_settings.cfg"

## "play" or "tutorial" — set by the menu before switching to the game scene.
static var mode := "play"


static func is_tutorial_done() -> bool:
	var cfg := ConfigFile.new()
	if cfg.load(SETTINGS_PATH) != OK:
		return false
	return bool(cfg.get_value("progress", "tutorial_done", false))


static func mark_tutorial_done() -> void:
	var cfg := ConfigFile.new()
	cfg.load(SETTINGS_PATH)   # missing file is fine — we create it
	cfg.set_value("progress", "tutorial_done", true)
	cfg.save(SETTINGS_PATH)
