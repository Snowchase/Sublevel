extends RefCounted
## Loads the game's art from res://assets/ (generated with Higgsfield, cut
## into sprites by tools/process_art.py). Every getter returns null when a
## file is missing or not yet imported, and callers fall back to the plain
## shapes — the game never breaks for lack of art.

const BASE := "res://assets/"
const STAFF_FILES := ["attendant", "security", "maintenance"]   # indexed by C.Role

static var _cache: Dictionary = {}


static func tex(relative_path: String) -> Texture2D:
	if _cache.has(relative_path):
		return _cache[relative_path]
	var path := BASE + relative_path
	var t: Texture2D = null
	if ResourceLoader.exists(path):
		t = load(path) as Texture2D
	_cache[relative_path] = t
	return t


static func vehicle(type_name: String) -> Texture2D:
	return tex("vehicles/%s.png" % type_name)


static func staff(role: int) -> Texture2D:
	if role < 0 or role >= STAFF_FILES.size():
		return null
	return tex("staff/%s.png" % STAFF_FILES[role])


static func incident_icon(type_name: String) -> Texture2D:
	return tex("icons/%s.png" % type_name)


static func tile(tile_name: String) -> Texture2D:
	return tex("tiles/%s.png" % tile_name)


static func prop(prop_name: String) -> Texture2D:
	return tex("props/%s.png" % prop_name)


static func menu_background() -> Texture2D:
	return tex("ui/menu_background.jpg")


## True when the floor textures are present (the renderer's art mode).
static func has_floor_art() -> bool:
	return tile("concrete") != null and tile("asphalt") != null and tile("wall") != null


## Every asset the game expects — used by the asset test.
static func expected_paths() -> Array[String]:
	var out: Array[String] = []
	for v in ["Compact", "Standard", "SUV", "Motorcycle", "Disabled", "DeliveryVan"]:
		out.append("vehicles/%s.png" % v)
	for s in STAFF_FILES:
		out.append("staff/%s.png" % s)
	for i in ["OilSpill", "FenderBender", "OverstayVehicle", "PowerOutage", "SuspiciousVehicle",
			"MedicalEmergency", "TheftInProgress", "StructuralCrack", "Altercation", "PipeBurst"]:
		out.append("icons/%s.png" % i)
	for t in ["concrete", "asphalt", "wall"]:
		out.append("tiles/%s.png" % t)
	for p in ["gate", "oil_stain", "crack", "pillar"]:
		out.append("props/%s.png" % p)
	out.append("ui/menu_background.jpg")
	return out
