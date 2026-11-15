# SSM6 DX supplemental mods

For the Traditional Chinese `SM6TC.exe` build, the combined package installs three supplied binaries:

- `d3dx9_43.dll` — the D3DX9 proxy mod loader. It loads DLL mods from the game’s `mods` directory; the loader documents per-mod `load.txt` ordering. [Loader project](https://github.com/techiew/EldenRingModLoader)
- `mods/Fix_for_SM6.dll` — retained unmodified. Its supplied log records two AOB-based code patches, but does not document their exact gameplay effects.
- `mods/resolution_for_SM6TC.dll` — retained unmodified. Its config sets the game’s internal render size to 2560×1440. MUSOUTweaksPC continues to scale the output to the current desktop size.

The supplied `enable.txt` files keep both mods enabled. Set a mod’s file to `0` to disable it. The resolution mod’s `load.txt` keeps its supplied order; `Fix_for_SM6.dll` uses the loader’s automatic order after explicitly ordered mods.

MUSOUTweaksPC remains the only DirectInput/XInput adapter. The supplied XInputPlus `Dinput8.dll`, `Dinput.dll`, `XInput1_3.dll`, and `XInputPlus.ini` are not installed because they provide a second controller proxy and overlap the existing adapter.

These supplemental DLLs were copied unchanged from the supplied `musou6dx` folder. The folder contains no source for them; the resolution config is encoded in GBK. They are installed only when `SM6TC.exe` is present, not for `SM6EN.exe` or `SM6JP.exe`.

SHA-256 identities of the supplied files:

- `d3dx9_43.dll`: `2181441725535c2b205b162ce7b683fafd558dfbb52907707eae99b92c322093`
- `Fix_for_SM6.dll`: `a8bb44c70d11e3d8305414301a2ab9157fa4558a23f7321603ea8e49b04fa20f`
- `resolution_for_SM6TC.dll`: `bc24bd97a6a472683d03591fc79558e5fb294447a91d09ee32694c492e8b32f5`
