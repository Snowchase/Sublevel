"""
balance_economy.py — Economy balance analysis and tuning.
Simulates revenue/expense curves given configurable parameters.
Run: python Tools/balance_economy.py
"""

import json
from pathlib import Path

DATA_DIR = Path(__file__).parent.parent / "Data"

# Revenue parameters (tune these)
HOURLY_RATE_BASE   = 4.00   # $ per stall per hour
SURGE_MULTIPLIER   = 2.0    # Applied during city events
LOAN_INTEREST_RATE = 0.08   # 8% annual, applied per in-game week

# Excavation costs per floor (in-game $)
EXCAVATION_COSTS = {
    "B2": 25000,
    "B3": 40000,
    "B4": 65000,
    "B5": 100000,
}

def simulate_daily_revenue(stall_count: int, occupancy_rate: float, hour_lambda: list) -> float:
    total_vehicle_hours = sum(hour_lambda) * occupancy_rate
    return total_vehicle_hours * HOURLY_RATE_BASE * stall_count / max(1, len(hour_lambda))

def main():
    with open(DATA_DIR / "VehicleArrivals.json") as f:
        arrivals = json.load(f)

    weekday_lambda = arrivals["weekday"]

    for stall_count in [30, 60, 100, 150]:
        daily = simulate_daily_revenue(stall_count, 0.7, weekday_lambda)
        print(f"  {stall_count:3d} stalls @ 70% occupancy → ${daily:,.2f}/day")

if __name__ == "__main__":
    main()
