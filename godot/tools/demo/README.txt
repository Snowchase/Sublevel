SUBLEVEL - playable demo
========================

You run an underground parking garage in a city that never sleeps.
Keep it open for 7 in-game days and finish with the highest net worth you can.

HOW TO START
  Windows : double-click SUBLEVEL.exe. Windows may say "Windows protected your PC"
            because the demo isn't code-signed: click "More info", then "Run anyway".
  Linux   : chmod +x SUBLEVEL.x86_64 && ./SUBLEVEL.x86_64
  Web     : the web build has to be served over http (it won't open from a file).
            Upload the zip to itch.io as an HTML game, or run a local server in
            this folder:  python -m http.server 8000   then open http://localhost:8000

FIRST TIME?  Pick "Tutorial" in the menu. It takes about 5 minutes.

CONTROLS
  Space            pause / resume          + / -     speed x1 to x16
  1 2 3 or click   answer a decision card  P         auto-pause on decisions
  F1 F2 F3         hire Attendant / Security / Maintenance (Shift = let one go)
  X                build the next section of the garage (+36 stalls)
  T / C            buy a ticket machine / a CCTV level
  , / .            lower / raise your price
  L                take a $5,000 loan
  WASD, wheel      pan, zoom               Tab       day-by-day summary
  M                main menu               F12       screenshot

HOW YOU LOSE
  - A day ends with your balance below -$2,000, or
  - you end three nights in a row below $0.
  The city also fines you every night you have no Attendant or no Security on duty.

FEEDBACK
  Every game writes a log you can send back with your notes:
    Windows: %APPDATA%\Godot\app_userdata\SUBLEVEL Playtest\playtests\
    Linux  : ~/.local/share/godot/app_userdata/SUBLEVEL Playtest/playtests/
