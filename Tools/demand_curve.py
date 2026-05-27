"""
demand_curve.py — Generates Data/VehicleArrivals.json from configurable parameters.
Run: python Tools/demand_curve.py
Output: Data/VehicleArrivals.json
"""

import json
import math
from pathlib import Path

# Base lambda (vehicles/minute) by hour of day
BASE_LAMBDA_WEEKDAY = [
    0.5, 0.2, 0.1, 0.1, 0.3, 1.2, 3.5, 6.0,   # 00:00 – 07:00
    4.5, 3.0, 2.5, 3.0, 5.0, 4.0, 3.0, 4.5,   # 08:00 – 15:00
    7.0, 8.0, 6.0, 3.5, 2.0, 1.5, 1.0, 0.7,   # 16:00 – 23:00
]

BASE_LAMBDA_WEEKEND = [
    0.4, 0.2, 0.1, 0.1, 0.2, 0.5, 1.0, 2.5,
    4.0, 5.5, 6.0, 6.5, 7.0, 7.5, 6.5, 5.5,
    5.0, 6.0, 7.0, 5.0, 3.0, 2.0, 1.5, 0.8,
]

# City event multipliers applied to the base lambda
CITY_EVENT_MULTIPLIERS = {
    "stadium_night":  3.5,
    "rainstorm":      1.8,
    "police_sweep":   0.4,
    "protest_block":  0.2,
    "concert_letout": 2.5,
    "power_strain":   1.0,
}

def generate_arrivals():
    output = {
        "_comment": "Base demand curve — vehicles per minute by hour. Edit via demand_curve.py",
        "weekday": BASE_LAMBDA_WEEKDAY,
        "weekend": BASE_LAMBDA_WEEKEND,
        "city_event_multipliers": CITY_EVENT_MULTIPLIERS,
    }

    out_path = Path(__file__).parent.parent / "Data" / "VehicleArrivals.json"
    with open(out_path, "w") as f:
        json.dump(output, f, indent=4)

    print(f"Written: {out_path}")

if __name__ == "__main__":
    generate_arrivals()
