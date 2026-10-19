# SUBLEVEL — Godot playtest build

A 2D, top-down playtest version of SUBLEVEL built in **Godot 4**. It runs the same
game rules as the Unreal build and reads the same design data (`../Data/*.json`),
so you can test whether the game is fun and balanced without waiting on Unreal
compiles. The Unreal code is untouched.

---

## 1. Run it (Windows)

1. Download **Godot 4.3 or newer — Standard edition** (not .NET) from
   <https://godotengine.org/download/windows/> and unzip it anywhere.
2. Open Godot. In the Project Manager click **Import**, browse to
   `Sublevel\godot\project.godot`, and click **Import & Edit**.
3. Press **F5** (or the ▶ button, top right) to play.

> Keep the `godot` folder next to the repo's `Data` folder. The game reads its
> rules from `..\Data\`. If you see a red "DATA ERROR" in the log, that's why.

---

## 2. Controls

| Key / mouse | Action |
|---|---|
| **Space** | Pause / resume |
| **P** | Toggle auto-pause: the game pauses when a decision card appears and resumes when you've answered them all (on by default, because card timers run on game time and expire in seconds at ×8) |
| **+ / −** | Speed ×1 · ×2 · ×4 · ×8 · ×16 |
| **1 / 2 / 3** or click | Choose a branch on the red decision card |
| **F1 / F2 / F3** | Hire Attendant / Security / Maintenance (hold **Shift** to let one go) |
| **L** | Take a $5,000 loan |
| **[ / ]** | Halve / double city demand (playtest knob) |
| **WASD / arrows**, right- or middle-drag | Pan |
| **Mouse wheel** | Zoom |
| **Mouse over a tile** | Shows what's there (bottom of screen) |
| **Tab** | Session summary (day-by-day table + every decision) |
| **R** (with summary open) | Restart with a new seed |
| **F12** | Screenshot |

The left panel's buttons do the same as the hire, fire, loan and speed keys.

**On screen:** gray = lanes, green **IN** / red **OUT** = gates, tinted squares = stall
types (teal compact, blue-gray standard, brown oversized, purple **M**otorcycle, blue
**D**isabled). Cars are colored by type, and a red dot means a car is stuck in traffic.
Circles are staff: **A**ttendant (yellow), **S**ecurity (blue), **M**aintenance (orange).
Pulsing rings are incidents (red = needs your decision, orange = waiting for staff,
yellow = being handled). Cars waiting on the street queue above the IN gate.

---

## 3. The goal

Keep the garage solvent for **7 in-game days** (one day is about 12 real minutes at
×1, or about 1.5 minutes at ×8). You go bankrupt if your balance ends a day below
**−$5,000**.

Money comes from parking fees: $4 per in-game hour, scaled by vehicle type, and ×1.5
during surge events. You pay staff wages daily (Attendant $80, Security $120,
Maintenance $100) and loan repayments. Decisions move three faction scores (City,
Police, Shadow), and those scores trigger city events such as police sweeps,
inspections and black-market nights.

---

## 4. Playtest script (about 30 minutes)

Play naturally first. Then answer these. Short notes are fine.

**Session A: learn it (×1–×2, first in-game day)**
1. Within 2 minutes, could you tell what was happening without reading this file?
2. Did the first decision card make sense? Did you feel you had enough information to choose?
3. Did you notice when an incident was waiting for staff that you didn't have?

**Session B: play to win (×4–×8, aim for day 7)**
4. Did hiring or firing ever feel like a real choice? Which role felt useless?
   (Attendants currently only patrol. That's a known open question.)
5. Were you ever short of money? When, and why?
6. Did any city event change how you played?
7. Did the lot ever fill up? Press **]** a couple of times to raise demand. When does it start to feel stressful?

**Session C: break it**
8. Ignore every decision for a whole day. What happens? Is that outcome fair?
9. Push demand to ×8. Does traffic jam? Do cars get stuck permanently (red dots that never clear)?

**What to send back:** your answers, plus the session log file (next section).

---

## 5. Session logs

Every session writes a JSON log. It's updated at the end of every in-game day, on
restart and on quit, and records each day's revenue, expenses, cars served, turned
away and lost, incidents, every decision you made or ignored, and your hire, fire,
loan and speed actions:

- **Windows:** `%APPDATA%\Godot\app_userdata\SUBLEVEL Playtest\playtests\`
- **Linux:** `~/.local/share/godot/app_userdata/SUBLEVEL Playtest/playtests/`

The exact path is also printed to Godot's Output panel when a session starts.

---

## 6. Automated tests and balance report

These run without a window. On Windows, point `GODOT` at the **console** executable
that came in the zip:

```bat
cd Sublevel\godot
set GODOT=C:\path\to\Godot_v4.x-stable_win64_console.exe
run_tests.bat              :: 18 tests
run_tests.bat --balance    :: balance table across demand levels
```

Linux/macOS: `GODOT=/path/to/godot ./run_tests.sh` (add `--balance` for the report).

The test suite checks:
- **Data and layout:** all JSON loads; 144 stalls of the right types; every stall reachable.
- **Pathing:** routes avoid walls, other stalls and blocked tiles.
- **Determinism:** the same seed gives the same game.
- **Vehicle lifecycle:** park, pay, leave; each vehicle type uses the right stall.
- **Decisions:** branches apply the JSON faction deltas and money; ignored cards fire the default branch; Security-only branches lock without a guard.
- **Staff and incidents:** maintenance clears blocking incidents; unattended incidents get fined; towing removes the car.
- **Economy:** day-end wages and loan math; bankruptcy.
- **City events:** faction-triggered and scheduled events; surge pricing.
- **Street queue:** overflow counts as lost cars.
- **UI smoke test:** the HUD and main scene load and run.
- **3-day soak:** bookkeeping invariants hold, no car is stuck for long, and the sim is fast enough for ×16.

The balance report simulates 3 days × 3 seeds at demand ×0.5, ×1, ×2 and ×4. It uses
a simple player policy (starter crew; answer every card after 10 s with the first
allowed choice) and prints revenue, profit, cars served, turned away and lost, peak
occupancy and incidents per day. Use `-- --days=7 --seeds=5` for a longer run.

Screenshot or demo mode, for checking visuals without playing:

```bat
"%GODOT%" --path . -- --seed=7 --warmup-ticks=3000 --autoplay --screenshot=shot.png
```

---

## 7. How this build differs from the Unreal build

These are deliberate. Most of them are what this playtest is meant to evaluate.

| Area | Unreal build | This playtest | Why |
|---|---|---|---|
| Parking fee | Bills **real** seconds, so every car pays the $1 minimum | Bills **in-game hours** | UE bug, filed separately. With the UE math, daily revenue is about $36 against $300 in wages. |
| Pathing | Per-destination flow fields | Godot's `AStarGrid2D` with congestion weights | Same behavior, much faster in GDScript |
| Visibility (§8) | Cameras and lights; with none placed, everything is visible | Everything is visible | No camera or light placement yet (needs build mode) |
| Start time | Midnight | 06:00 | Midnight to 06:00 is nearly empty. Bad first impression. |
| Street queue | Arrivals at a full gate are dropped | Up to 8 cars wait on the street for 15 s, then leave ("lost at gate") | Makes demand pressure visible and measurable |
| Security role | Patrols only | Branches labelled "Security…" need a guard on staff, and a guard walks to the scene | Hypothesis: gives Security a purpose |
| Unattended incidents | Never expire | Auto-resolve incidents left 2 in-game hours with no staff are fined $50 plus their JSON expiry delta | Makes the Maintenance hire matter |
| Idle maintenance | — | Slowly repairs structural integrity | Gives Maintenance a job between incidents |
| City inspection | Broadcast only | Fine of $500 and City −5 if integrity is below 50% | Implements the JSON note |

Not in this build yet: excavation and multiple floors (§9), build mode (§13), CCTV
view, save/load.

---

## 8. Tuning

- **Design data:** edit `../Data/*.json`. These changes apply to both builds.
- **Playtest-only knobs:** `scripts/sim/sim_const.gd`, section "Playtest tuning".
  Values tagged `(UE)` match the Unreal build. The rest are the hypotheses above.

## Layout

```
godot/
  project.godot, scenes/main.tscn, icon.svg
  scripts/sim/   — pure simulation (no nodes): runs in-game and headless
  scripts/game/  — main scene, floor renderer, HUD, session logger
  tests/         — headless test suite and balance report
```
