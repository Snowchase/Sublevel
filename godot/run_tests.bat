@echo off
rem Runs the SUBLEVEL playtest test suite headlessly (Windows).
rem   run_tests.bat                      all tests
rem   run_tests.bat --only=test_layout   one test
rem   run_tests.bat --balance            balance report instead of tests
rem Set GODOT to your Godot *console* exe, e.g.
rem   set GODOT=C:\Godot\Godot_v4.5-stable_win64_console.exe
setlocal
if "%GODOT%"=="" set "GODOT=godot"
cd /d "%~dp0"

if "%~1"=="--balance" (
  "%GODOT%" --headless --path . --script res://tests/balance_report.gd -- %2 %3 %4
) else (
  "%GODOT%" --headless --path . --script res://tests/run_tests.gd -- %*
)
exit /b %ERRORLEVEL%
