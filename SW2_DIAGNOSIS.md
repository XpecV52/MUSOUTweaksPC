# Sengoku Musou 2 startup diagnosis

## Result

The startup crash is reproducible without this patch. `SW2.exe` reports an access violation (`0xc0000005`) at offset `0x001807fe`, with `SW2.exe` listed as the faulting module. The archived Windows Error Reporting records show the system `C:\Windows\System32\dinput8.dll`, not this project's proxy.

The exact local `SW2.exe` (SHA-256 `42143BC743CE760DF282B9CADA91CF4E0B6E4203C82CBA4A933E1639F150787B`) contains the SafeDisc marker `BoG_ *90.0&!! Yy>` and embedded `SECDRV.SYS`/`SecDrv` names. On this Windows 11 installation, `C:\Windows\System32\drivers\secdrv.sys` is absent and the `secdrv` service is not installed. Microsoft documents that `secdrv.sys` is used by SafeDisc games and that disabling it can stop older games from running.

The test build of the local `dinput8.dll` adds a separate compatibility conflict with this game. It has been removed from the game directory, and the installer intentionally skips this title. The generated INI and log were also removed because they did not exist before installation.

Windows 7, Vista SP2, and XP SP3 compatibility settings did not fix the crash. One run displaying a game window briefly was initially mistaken for a successful start; it crashed several seconds later at the same exception offset. There is no confirmed compatibility-mode fix.

## Windows compatibility cause

The official Koei troubleshooting page lists DirectX 9.0c and Windows Media Format 9 as dependencies. The installed system already has the legacy D3DX9 components and WMV core module, so simply installing those runtimes is not supported by the current evidence as a fix.

The primary compatibility failure is the SafeDisc authorization path: this copy expects the legacy `secdrv.sys` driver, which is absent from this Windows 11 installation. That matches the game's crash before reaching the menu. The title's official support page lists Windows XP/Vista era DirectX 9.0c and Windows Media Format 9 dependencies, and does not list Windows 11.

## Safe current state

- No project `dinput8.dll`, `d3d9.dll`, or `winmm.dll` proxy is installed in the game directory.
- No per-game compatibility registry override is configured by this project.
- Do not install or enable the vulnerable `secdrv.sys` driver on Windows 11 as a workaround. Microsoft explicitly warns that re-enabling it reduces security.
- There is no safe native Windows 11 fix for this copy protection path. The safe route is to run the licensed original release in a suitably isolated older Windows environment with the original media; compatibility mode alone is not a fix. This controller/window DLL should stay uninstalled in this game folder.

## References

- [Koei official troubleshooting: DirectX 9.0c and Windows Media Format 9](https://www.gamecity.ne.jp/regist_c/user/sengoku2/trouble.htm)
- [Microsoft MS07-067: SafeDisc games use `secdrv.sys`](https://learn.microsoft.com/en-us/security-updates/securitybulletins/2007/ms07-067)
- [Microsoft MS15-097 / KB3086255: disabling `secdrv.sys` may stop older games; re-enabling it lowers security](https://support.microsoft.com/en-us/servicing/os/windows-server/2015/09/ms15-097-description-of-the-security-update-for-the-graphics-component-in-windows-september-8-2015)
