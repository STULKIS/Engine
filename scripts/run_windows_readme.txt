Derecho Engine - quick start
=============================

1. Start the game:
     run.bat                 keeps the console window open (recommended)
     derecho.exe            start directly

2. Check that this machine can run it:
     derecho.exe --selftest
   Renders 45 frames, inspects the pixels, writes screenshots\selftest.ppm
   and prints [PASS]/[FAIL] for each check. Exit code 0 = OK, 3 = failed.

3. If nothing happens or it closes immediately:
     - run run.bat instead of double-clicking the exe and read the message
     - open derecho.log in this folder (written on every run)
     - make sure assets\ and SDL2.dll are in this folder, next to the exe
     - an antivirus may have quarantined derecho.exe; allow/restore it
     - the GPU driver must support OpenGL 4.5; avoid remote desktop sessions

4. Low-end machines / older GPUs:
     derecho.exe --safe-mode    no MSAA, no HDR, no bloom, 1024 shadow map,
                                 lighter vegetation and 0.85 render scale

5. Controls: WASD move, mouse look, Space jump, Shift sprint, Ctrl walk,
   F1 debug panel, F2 wireframe, F3 shadows, F4 foliage, F5 water,
   F6 time pause, [ / ] scrub time, R respawn, G new world, F11 fullscreen,
   F12 screenshot, Esc release mouse (again to quit).
