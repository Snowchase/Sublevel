#!/usr/bin/env bash
# Runs the SUBLEVEL playtest test suite headlessly.
#   ./run_tests.sh                      all tests
#   ./run_tests.sh --only=test_layout   one test
#   ./run_tests.sh --balance            balance report instead of tests
# Set GODOT to your Godot 4 executable if it isn't on PATH as "godot".
set -euo pipefail
GODOT="${GODOT:-godot}"
cd "$(dirname "$0")"

if [[ "${1:-}" == "--balance" ]]; then
  shift
  exec "$GODOT" --headless --path . --script res://tests/balance_report.gd -- "$@"
fi
exec "$GODOT" --headless --path . --script res://tests/run_tests.gd -- "$@"
