# Multi-game build

`build/dinput8.dll` contains the XInput/native-DS4 DirectInput adapter and platform hooks. The DLL selects the config filename from the game executable. It is installed for:

- `Shin Sangokumusou 3\game` — reads `SSM3_Tweaks.ini`; D-pad directions are also mirrored to the left stick by default.
- `Shin Sangokumusou 4` — reads `SSM4_Tweaks.ini`.

`build/dinput8-platform.dll` contains platform hooks and is installed in `Dynasty Warriors 8` (真三国无双7). `Launch.exe` reads `SSM7_Tweaks.ini`; the D3D9 backbuffer matches the active monitor while the game's logical render size is preserved.

`OROCHI_Z_TC.exe` (无双蛇魔Z) continues to use `OrochiZPadX.ini`.

`Sengoku Musou 2` uses `build/dinput8.dll` and `SM2_Tweaks.ini`. The config enables XInput/native DS4 adaptation, removes the window-close confirmation, and paces the game's D3D9 presents to 75 FPS. It keeps the current game render size and does not change the Windows desktop resolution or refresh rate. The original EXE started after its missing PC `linkdata` assets were restored; no system security or SafeDisc setting was changed. See `SW2_DIAGNOSIS.md`.

Run `build.cmd` and `build-platform.cmd` from a Visual Studio x86 developer environment to rebuild both variants. `install-targets.ps1` installs only the compatible targets and creates timestamped backups with `manifest.json`.
