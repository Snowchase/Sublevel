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
3. Press **F5** (or the ▶ button, top right). The **main menu** appears:
   - **Tutorial**: a guided 10-step walkthrough on a tiny 12-stall garage
     (about 5 minutes). Start here the first time.
   - **New Game**: the real 7-day game: a 36-stall garage with room to grow to 144.
   - **How to Play**: rules and controls at a glance.
   - **Quit**.

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
| **X** | Build the next section of the garage (+36 stalls) |
| **T** / **C** | Buy a ticket machine / a CCTV level |
| **, / .** | Lower / raise your price ($1.50–$6.00 an hour, in 50¢ steps) |
| **[ / ]** | Halve / double city demand (playtest knob) |
| **WASD / arrows**, right- or middle-drag | Pan |
| **Mouse wheel** | Zoom |
| **Mouse over a tile** | Shows what's there (bottom of screen) |
| **Tab** | Session summary (day-by-day table + every decision) |
| **R** (with summary open) | Restart with a new seed |
| **M**, or the **Main menu** button | Back to the main menu |
| **Enter** | Tutorial: go to the next step |
| **F12** | Screenshot |

The left panel's buttons do the same as the hire, fire, loan, grow, price and speed keys.

**On screen:** gray = lanes, green **IN** / red **OUT** = gates, tinted squares = stall
types (teal compact, blue-gray standard, brown oversized, purple **M**otorcycle, blue
**D**isabled). Cars are colored by type, and a red dot means a car is stuck in traffic.
Circles are staff: **A**ttendant (yellow), **S**ecurity (blue), **M**aintenance (orange).
Pulsing rings are incidents (red = needs your decision, orange = waiting for staff,
yellow = being handled). Cars waiting on the street queue above the IN gate.

---

### The tutorial

The tutorial runs on a 12-stall garage with no random events. A panel at the
top explains each step, and a pulsing gold arrow points at the relevant tile.
Each step completes when you actually do the thing:

1. **Welcome:** the IN and OUT gates.
2. **Time controls:** start the clock with +.
3. **Customers arrive:** three cars find stalls that fit.
4. **Getting paid:** a car leaves and pays.
5. **Incidents:** an oil spill blocks a lane, and your maintenance worker cleans it.
6. **Your first decision:** the game pauses, and you choose an option.
7. **Hiring staff:** hire a second maintenance worker.
8. **Rush hour:** more cars than stalls, and reputation reacts.
9. **Your day at a glance:** open the summary with Tab.
10. **You're ready:** start a real game, or go back to the menu.

Finishing it puts a ✓ next to Tutorial in the menu. **Exit to menu** leaves at any time.

---

## 3. The goal

Survive **7 in-game days** with the highest **net worth** you can (one day is about
12 real minutes at ×1, or about 1.5 minutes at ×8). Net worth is your cash, minus
what you owe the bank, plus 80% of everything you spent building the garage.
At the end you get a rating: **Bronze** from $3,000, **Silver** from $7,000, **Gold**
from $12,000.

**Money.** You start with $2,500 and 36 stalls on a floor built for 144. Cars pay
$3 per in-game hour by default, scaled by vehicle type, and ×1.5 during surge events.
Every midnight you pay **rent**, staff wages (Attendant $80, Security $120,
Maintenance $100) and any loan repayment. Rent is $388 plus $10 per built stall
($748 on day 1) and rises **5% every day**, so a garage that stands still slowly
loses ground. The bank lends at most $10,000 in total (L key, $5,000 at a time).

**Growing.** Money buys growth:
- **Expansion (X):** opens the next section of the floor, +36 stalls each, for
  $2,000, then $4,500, then $8,000. A bigger garage draws more drivers
  (demand × (stalls / 36)^0.9), but each stall adds rent and more incidents.
- **Ticket machine (T, $1,500):** the gate keeps moving without an attendant
  (4 s → 1.25 s per car) and is faster with one.
- **CCTV (C, $1,200 per level, 2 levels):** each level cuts crime incidents by 30%.
- **Price (, and .):** a higher price earns more per car but drives customers
  away (demand × ($3 / price)^1.3). It pays off when the garage is overflowing.

**Ways to lose.** You go bankrupt if a day ends below **−$2,000**, or if you end
**three nights in a row below $0** (the panel warns you after the first). The city
fines you **$300** for each night with no Attendant or no Security on duty (and it
costs reputation and City standing). A theft with no guard on the payroll costs an
extra $200.

**Reputation (0–100)** decides how many customers come: 0 means ×0.6 demand, 50
means ×1.0, and 100 means ×1.4.
- Each paying customer raises it a little.
- It drops when a car is turned away for lack of a stall (−1) or gives up in the
  street queue (−0.5).
- It drops when you ignore a decision card (−3), when an incident is left until the
  city fines you (−3), and when a theft goes through (−3).

**Your staff each have a job:**
- **Attendants** run the entry gate. With none, each car takes 4 s to process and
  rush-hour queues spill into the street. With one it's 1 s, and with two 0.5 s.
- **Security** cuts thefts, suspicious vehicles and altercations by 40% per guard
  (down to 30% of normal). Card choices labelled "Security…" also need a guard.
- **Maintenance** clears spills, crashes and cracks, and slowly repairs the
  structure when idle.

**Factions.** Decisions move three faction scores (City, Police, Shadow). Each score
drifts 15% back toward 0 every midnight. Sustained behavior triggers city events
such as police sweeps, inspections and black-market nights.

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
run_tests.bat              :: about 40 tests
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
- **Economy:** day-end rent, wages and loan math; loan cap; bankruptcy (hard floor and three red nights); faction drift.
- **Growth and pressure:** expansion opens the next section and adds rent; ticket machine, CCTV and pricing; understaffing fines; theft without a guard; net worth and rating.
- **Tension mechanics:** reputation scales demand; attendants speed up the gate; guards cut crime; incident rates scale with lot size.
- **City events:** faction-triggered and scheduled events; surge pricing.
- **Street queue:** overflow counts as lost cars.
- **UI smoke test:** the HUD and main scene load and run.
- **Menu and tutorial:** the menu builds and routes to the right mode, and a scripted player completes all 10 tutorial steps.
- **3-day soak:** bookkeeping invariants hold, no car is stuck for long, and the sim is fast enough for ×16.

The balance report simulates 5 full days × 3 seeds at demand ×0.5–×3 for two players:
an attentive one (answers every card) and a neglectful one (ignores them). It prints
profit, cars served, turned away and lost, peak and average occupancy, reputation
and bankruptcies, then marks each tension target HIT or MISS (see section 8). Use
`-- --days=7 --seeds=5` for a longer run.

Screenshot or demo mode, for checking visuals without playing:

```bat
"%GODOT%" --path . -- --seed=7 --warmup-ticks=3000 --autoplay --screenshot=shot.png
```

Screenshots need a real window; they don't work with `--headless`. Add
`--spawn-incident=SuspiciousVehicle` to capture a decision card, or
`--show-summary` to capture the summary screen. The menu is the start scene,
so pass `res://scenes/main.tscn` before `--` to screenshot the game. Add
`--mode=tutorial` (plus `--tutorial-skip-to=N` to jump to step N) for the
tutorial.

**CI:** `.github/workflows/godot-playtest.yml` runs the tests, the balance report and
nine screenshots (menu, tutorial and gameplay) on every push that touches `godot/` or `Data/`. Open the run on
GitHub's **Actions** tab. The summary page shows the test and balance tables, and
the **godot-playtest-results** download has the screenshots and session logs.

---

## 7. How this build differs from the Unreal build

These are deliberate. Most of them are what this playtest is meant to evaluate.

| Area | Unreal build | This playtest | Why |
|---|---|---|---|
| Parking fee | Bills **real** seconds, so every car pays the $1 minimum | Bills **in-game hours** | UE bug, filed separately. With the UE math, daily revenue is about $36 against $300 in wages. |
| Pathing | Per-destination flow fields | Godot's `AStarGrid2D` with congestion weights | Same behavior, much faster in GDScript |
| Visibility (§8) | Cameras and lights; with none placed, everything is visible | Everything is visible | No camera or light placement yet (needs build mode) |
| Start time | Midnight | 06:00 | Midnight to 06:00 is nearly empty. Bad first impression. |
| Starting lot | 24×16, 144 stalls | Same floor, but only the top **36 stalls** are built; the rest opens in three paid sections | Start small and earn your growth |
| Economy | $10,000 start, $4/h, no rent, bankrupt below −$5,000, unlimited loans | $2,500 start, **$3/h** (player-set), **rent $388 + $10/stall, +5%/day**, bankrupt below **−$2,000** or after **3 red nights**, loans capped at $10,000 | Margins thin enough that mistakes matter, and standing still loses |
| Upgrades | — | Expansion, ticket machine, CCTV, pricing; score = net worth | Gives money a purpose |
| Staffing | Optional | City fines $300/night per missing Attendant or Security | Firing everyone isn't a free win |
| Demand | `VehicleArrivals.json` as-is | Curve ×1.7 (`DEMAND_CURVE_SCALE`), then × **reputation** (×0.6–×1.4) | Fills the small lot at rush hour; service quality feeds back into demand |
| Attendants | Patrol only | Run the entry gate (4 s → 1 s → 0.5 s per car) | Gives the role a purpose |
| Factions | Accumulate forever | Drift 15% toward 0 each midnight | Events reflect recent behavior, not one old choice |
| Incident rates | Fixed | Scale with lot size; guards cut crime | A small garage isn't flooded with cards |
| City-event bursts | 50 cars | Scaled to lot size (about 13 cars for 36 stalls) | A concert can't wreck reputation unavoidably |
| Street queue | Arrivals at a full gate are dropped | Up to 8 cars wait on the street for 15 s, then leave ("lost at gate") | Makes demand pressure visible and measurable |
| Security role | Patrols only | Cuts crime incident rates; branches labelled "Security…" need a guard, who walks to the scene | Gives Security a purpose |
| Unattended incidents | Never expire | Auto-resolve incidents left 2 in-game hours with no staff are fined $50 plus their JSON expiry delta | Makes the Maintenance hire matter |
| Idle maintenance | — | Slowly repairs structural integrity | Gives Maintenance a job between incidents |
| City inspection | Broadcast only | Fine of $500 and City −5 if integrity is below 50% | Implements the JSON note |

Not in this build yet: multiple floors and excavation (§9; expansion here stays on
one floor), build mode (§13), CCTV view, save/load.

---

## 8. Automated balance results

Measured by the headless balance report on Godot 4.5.1 in CI. Setup: 40-stall lot,
**6 seeds × 5 full days** per row, starter crew. Two simulated players:
**attentive** answers every card after about 10 s with the best-paying option;
**neglectful** never answers a card.

| Demand | Attentive profit/day | Neglectful profit/day | Rush-hour peak | Turned away + lost/day (attentive) | Reputation (attentive / neglectful) |
|---|---|---|---|---|---|
| ×0.5 | −$175 | −$450 | 58% | 3 | 94 / 3 |
| **×1** | **+$356** | **−$72** | **76%** | **13** | **79 / 14** |
| ×1.5 | +$707 | +$298 | 88% | 29 | 61 / 23 |
| ×2 | +$764 | +$502 | 92% | 50 | 34 / 9 |

All four tension targets are met at ×1:
- Careful play earns a thin profit ($50–400 a day).
- Rush hour fills the lot to at least 75%.
- Some cars (1–15 a day) are turned away or lost.
- Neglect loses money.

How it got here (each round was a CI run):
1. **Smaller lot only:** reputation collapsed to about 0 for everyone. Concert
   crowds sized for 144 stalls (50 cars) swamped the street queue and cost
   reputation the player couldn't prevent. Event crowds now scale with lot size.
2. **Rebalanced reputation:** 3 of 4 targets hit, but rush hour peaked at only 62%.
3. **Demand ×1.7, $3/hour:** rush hour at 81%, but careful play earned $457 a day,
   above the $400 ceiling.
4. **Rent $750:** all four targets hit.

Above ×1.5 demand, both players make money, because the lot is full either way.
Reputation pulls demand back down as overflow grows. Whether that feels good is a
question for human playtesting.

Still open: neglect loses money (about $430 a day worse than careful play), but
only slowly. No simulated run went bankrupt within 5 days, and with only 3 seeds
the neglectful player's result swung from −$9 to −$173 a day, which is why CI now
uses 6. If losing should feel more threatening, the next levers are a bigger
reputation penalty for ignored cards or harsher default outcomes in
`IncidentTypes.json`. A human who also fires
staff or takes loans may find a different story.

Re-run after any change: `run_tests.bat --balance` (Windows) or
`./run_tests.sh --balance`. The report prints HIT or MISS for each target.

## 9. Tuning

- **Design data:** edit `../Data/*.json`. These changes apply to both builds.
- **Playtest-only knobs:** `scripts/sim/sim_const.gd`, section "Playtest tuning".
  Values tagged `(UE)` match the Unreal build. The rest are the hypotheses above.

## Art

The sprites, textures and menu art were generated with **Higgsfield** (GPT Image
2.6, "clean illustrated" style) as four sheets, then cut into game assets by a
script:

| Source sheet (`assets/source/`) | Becomes |
|---|---|
| `vehicles_sheet.png` | `assets/vehicles/*.png`: 6 top-down cars, facing right |
| `staff_icons_sheet.png` | `assets/staff/*.png` (3 roles) and `assets/icons/*.png` (10 incident icons) |
| `environment_sheet.png` | `assets/tiles/` (seamless concrete and asphalt, wall) and `assets/props/` (gate, oil stain, crack, pillar) |
| `menu_keyart.png` | `assets/ui/menu_background.jpg` |

The full set cost **4.5 credits**: two low-quality vehicle tests at 0.25 each, two
medium sheets at 0.50 each, and the high-quality key art at 1.50, plus one 1.50
edit of the key art. The first vehicle test came out in a three-quarter view, so
the second one forced a strict overhead view. The key-art edit turned the blue
accessible van, which had been drawn side-on, to match the other parked cars.

- **Replace an image:** drop a new sheet into `assets/source/` with the same file
  name, then run `python3 godot/tools/process_art.py` (needs `pip install pillow`).
  The script finds each sprite automatically, so new sheets don't have to match an
  exact grid. They only need the same number of items per row.
- **Fallback:** if any asset is missing, the game draws that object with the
  original plain shapes, so a half-finished art swap never breaks the game.
  `assets/source/` holds a `.gdignore` file, so Godot doesn't import the large
  originals.
- **Licensing:** check Higgsfield's terms before you sell the game with these assets.

## Layout

```
godot/
  project.godot, scenes/main.tscn, icon.svg
  scripts/sim/   — pure simulation (no nodes): runs in-game and headless
  scripts/game/  — main scene, floor renderer, HUD, session logger
  tests/         — headless test suite and balance report
```
