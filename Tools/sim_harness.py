"""
sim_harness.py — Headless simulation test harness.
Runs the incident scheduler and vehicle arrival model without UE5.
Used for balance testing before editor integration.
Run: python Tools/sim_harness.py
"""

import json
import math
import random
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional

DATA_DIR = Path(__file__).parent.parent / "Data"
SIM_TICK_INTERVAL = 0.05   # seconds (20 Hz)
TICKS_PER_DAY     = 14400  # configurable in SimulationSubsystem.cpp


@dataclass
class PendingEvent:
    scheduled_tick: int
    incident_type: str
    floor_index: int
    severity: float

    def __lt__(self, other):
        return self.scheduled_tick < other.scheduled_tick


def poisson_interval(lambda_per_sec: float, rng: random.Random) -> int:
    """Inter-arrival ticks for a Poisson process with given lambda."""
    u = max(rng.random(), 1e-9)
    seconds = -math.log(u) / lambda_per_sec
    return max(1, int(seconds / SIM_TICK_INTERVAL))


def run_sim(days: int = 7, seed: int = 42):
    rng = random.Random(seed)

    with open(DATA_DIR / "IncidentTypes.json") as f:
        incident_defs = {d["type"]: d for d in json.load(f)}

    # Seed initial event queue
    import heapq
    event_queue = []
    current_tick = 0
    incidents_fired = 0

    for incident_type, defn in incident_defs.items():
        lam = defn.get("lambda", 0.005)
        delay = poisson_interval(lam, rng)
        heapq.heappush(event_queue, PendingEvent(delay, incident_type, 0, rng.uniform(0.3, 1.0)))

    max_ticks = TICKS_PER_DAY * days

    while current_tick < max_ticks:
        current_tick += 1

        while event_queue and event_queue[0].scheduled_tick <= current_tick:
            evt = heapq.heappop(event_queue)
            incidents_fired += 1

            lam = incident_defs[evt.incident_type].get("lambda", 0.005)
            next_tick = current_tick + poisson_interval(lam, rng)
            heapq.heappush(event_queue, PendingEvent(next_tick, evt.incident_type, 0, rng.uniform(0.3, 1.0)))

    print(f"Sim complete — {days} days ({max_ticks} ticks), seed={seed}")
    print(f"  Incidents fired: {incidents_fired}")
    print(f"  Avg per day: {incidents_fired / days:.1f}")


if __name__ == "__main__":
    run_sim()
