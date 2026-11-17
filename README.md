# 老无双系列 PC 修复补丁 / MUSOUTweaksPC

<a id="中文"></a>
**中文** | [English](#english)

32 位 `dinput8.dll` 代理补丁，为光荣「无双」系列早期 PC 移植版补上现代手柄支持、窗口输入兼容和分辨率扩展。同一个 DLL 按游戏可执行文件名自动选择对应的 INI 配置，放进游戏目录即可生效，不改注册表、不写系统全局钩子。

## 支持的游戏

| 游戏 | 可执行文件 | 配置文件 | 补丁作用 |
| --- | --- | --- | --- |
| 真三国无双3(SSM3_Shin Sangokumusou3) | `ShinSangokumusou3.exe` | `SSM3_Tweaks.ini` | 手柄适配、十字键、无边框窗口、输入法隔离 |
| 真三国无双4 Special(SSM4SP_Shin Sangokumusou4 Special) | `SM4_WIN.exe` / `SS4*.exe` | `SSM4_Tweaks.ini` | 手柄适配、十字键菜单导航、无边框窗口 |
| 真三国无双6with猛将传DX(SSM6DX_Shin Sangokumusou6 with Moushouden DX) | `SM6TC.exe` / `SM6EN.exe` / `SM6JP.exe` | `SSM6_Tweaks.ini` | 手柄适配、十字键菜单导航、无边框全屏缩放、后台扫描器崩溃修复 |
| 真三国无双7with猛将传(SSM7DX_Shin Sangokumusou7 with Moushouden) | `Launch.exe` | `SSM7_Tweaks.ini` | 无边框全屏、清晰缩放、内部渲染分辨率、3D 镜头远近 |
| 战国无双2(SM2_SengokuMusou2) | `SW2.exe` | `SM2_Tweaks.ini` | 手柄适配、无边框窗口、引擎分辨率移植、HUD 校正、帧率限制 |
| 无双蛇魔Z(MOZ_MusouOrochi Z) | `OROCHI_Z_TC.exe` | `OrochiZPadX.ini` | 手柄适配、无边框窗口、3840×2160 独立渲染、镜头远近 |

## 各补丁具体做什么

### 真三国无双3(SSM3_Shin Sangokumusou3)

XInput 与原生 Sony DS4 手柄转成游戏能识别的虚拟 DirectInput 手柄，支持热插拔和「仅前台生效」；十字键输出为四个独立按键（Button 13–16）并另提供 POV 方向帽，不合并到左摇杆，避免推十字键时角色乱跑。窗口侧提供无边框全屏、游戏内输入法隔离、跳过关闭确认框。

**分辨率保持游戏自身 1280×960**，只由无边框窗口放大，任何高分辨率覆写都会被拒绝——这一作的引擎画布和 UI 是按固定尺寸写死的，仅改 D3D 后备缓冲区会裁切画面。

### 真三国无双4 Special(SSM4SP_Shin Sangokumusou4 Special)

与真三3 同一套手柄与窗口处理，另外通过代码指纹校验定位了本作的原生菜单映射表，因此十字键可以在游戏菜单里移动光标。分辨率策略与真三3 相同：保持游戏自身渲染目标，拒绝高分辨率覆写。

### 真三国无双6with猛将传DX(SSM6DX_Shin Sangokumusou6 with Moushouden DX)

手柄适配 + 十字键菜单导航 + 无边框全屏 + 单次主交换链呈现（让 Steam/RTSS 按一帧统计，覆盖层不掉帧计数），画面由无边框窗口拉伸铺满桌面。修复了本作后台扫描线程在特定时序下的崩溃。繁体中文版（`SM6TC.exe`）可额外安装附带的第三方 MOD：`d3dx9_43.dll` 加载器 + `Fix_for_SM6.dll`（修复面部表情），由 `install-ssm6dx.ps1` 自动备份后安装，细节见 `THIRD_PARTY_MODS.md`。

### 真三国无双7with猛将传(SSM7DX_Shin Sangokumusou7 with Moushouden)

这一作使用平台专用构建 `dinput8-platform.dll`（游戏本身已支持 XInput）。提供无边框全屏、单次 Present、Catmull-Rom 清晰缩放；可选内部渲染分辨率（默认 3840×2160，由 `internal_render_width/height` 控制，输出仍是桌面尺寸）；3D 镜头远近可调（`camera_distance_percent`，50–300，100 为原始），并支持**视野调整组合键**。

### 战国无双2(SM2_SengokuMusou2)

手柄适配 + 十字键 + 无边框窗口 + 跳过关闭确认框。另有一条独立的引擎分辨率移植路径：等待受保护的可执行文件解密后，在内存中替换宽高查询函数并校正 HUD/菜单几何，`0/0` 表示跟随当前显示器物理分辨率。帧率限制默认 **65 FPS**（`fps_limit=65`），不修改 Windows 桌面分辨率或刷新率。

### 无双蛇魔Z(MOZ_MusouOrochi Z)

手柄适配（含原生 DS4）+ 无边框窗口 + **3840×2160 独立引擎渲染**（走游戏主交换链输出到桌面，保留视频背景），并针对高分辨率带来的两个渲染问题做了修正：GS 精灵 Y 坐标超过 2048 像素时的回绕，以及全屏背景把深度缓冲区写满导致后续 3D 地图/读盘/结算界面被遮挡。同样支持 3D 镜头远近与**视野调整组合键**。

## 视野调整组合键

蛇魔Z 与真三7 支持在游戏内实时调整 3D 镜头远近，默认组合键：

| 组合键 | 作用 |
| --- | --- |
| `Ctrl + Alt + PageUp` | 镜头拉远（每次 +5%，上限 300%） |
| `Ctrl + Alt + PageDown` | 镜头拉近（每次 −5%，下限 50%） |
| `Ctrl + Alt + Home` | 恢复原始 100% |

只移动视点，不改 FOV、人物比例和 HUD 缩放；仅在游戏位于前台时响应。调整结果会自动写回 INI 的 `camera_distance_percent`，下次启动沿用。按键、步进和修饰键都可在配置里改（`camera_hotkey_farther/nearer/reset`、`camera_distance_step`、`camera_hotkey_modifiers`），改完约 500 毫秒生效，转动一下视角即可刷新已缓存的镜头。蛇魔Z 的准备界面地图保持原有取景。

## 安装

1. 从 [Releases](../../releases) 取 `MUSOUTweaksPC_V<版本>.zip`，解压后里面按游戏分好了子包，只解自己那一个（子包内就是 `dinput8.dll` + 对应 INI）。
2. 把对应游戏的 `dinput8.dll` 与 `*.ini` 放进该游戏可执行文件所在目录：

| 游戏 | 目标目录 | DLL |
| --- | --- | --- |
| 真三3 | `Shin Sangokumusou 3\game` | `dinput8.dll` |
| 真三4 Special | `Shin Sangokumusou 4` | `dinput8.dll` |
| 真三6 DX | `Dynasty Warriors 7 DX` | `dinput8.dll` |
| 真三7 | `Dynasty Warriors 8` | `dinput8-platform.dll`（改名 `dinput8.dll`） |
| 战国无双2 | 游戏根目录 | `dinput8.dll` |
| 无双蛇魔Z | 游戏根目录 | `dinput8.dll` |

3. 真三6 DX 可直接运行 `install-ssm6dx.ps1`，它会做时间戳备份并顺带装第三方 MOD；恢复用 `restore.ps1`。
4. 想撤销：删掉放进游戏目录的 `dinput8.dll` 与 INI 即可，游戏恢复原状。

## 配置

所有 INI **每一条都有中文和英文注释**，键名保留英文。修改后需完全退出并重新启动游戏。缺项自动取默认值，超范围数值会被夹到有效区间。

- **日志默认全关**：`log=0`。需要排障时改成 `log=1` 才会生成 `MUSOUTweaksPC.log`。
- `[padfix]`：`enable` 手柄适配总开关，`backend` 选择自动/XInput/原生 DS4，`dpad_*` 控制十字键行为，其余为摇杆死区、扳机阈值、连接保护。
- `[platform]`：`window_mode` 无边框窗口、`single_present` 单次呈现、`disable_game_ime` 输入法隔离、`com_apartment_compat` COM 兼容、`close_without_confirmation` 跳过关闭确认、`fps_limit` 帧率上限。
- 分辨率相关键按游戏不同：真三3/4 会拒绝覆写；真三6 DX、真三7、蛇魔Z、战国2 使用各自已核验的引擎路径。

逐项说明见 `CONFIGURATION.md`，多游戏差异见 `MULTI_GAME.md`。

## 从源码构建

需要 Visual Studio 的 x86 C++ 工具链：

```bat
build.cmd           :: 生成 build\dinput8.dll（含手柄适配）
build-platform.cmd  :: 生成 build\dinput8-platform.dll（仅平台层）
python package.py   :: 打包 build\dinput8.dll 与 dinput8-platform.dll
```

## 第三方组件

真三6 DX 繁体版附带的 MOD 来源与授权见 `THIRD_PARTY_MODS.md`。

## 更新记录

### 2.45
- **真三国无双6 with 猛将传 DX**：十字键改用「游戏自己的键盘设备」导航菜单，`dpad_as_left_stick` 恢复默认 0。诊断确认该作是以**缓冲模式**读 DirectInput 键盘（`GetDeviceData`，容量 64）而不是轮询 `GetDeviceState`，所以新配置项 `dpad_arrow_keys` 在游戏的键盘设备上成对追加 `DIK_UP/DOWN/LEFT/RIGHT` 的按下与松开事件。菜单光标可以移动，左摇杆完全不动，**角色不会再跟着走**。
- **真三国无双6 with 猛将传 DX**：`native_resolution` / `scale_to_fill` 改为默认 0。原生渲染下游戏只往 2560×1440 后备缓冲的左上角 1920×1080 区域绘制，右边和下边是黑的；开启 `scale_to_fill` 又会让 3D 视口与按 1920×1080 排版的 2D 层错位，表现为「视野异常」。现在保留游戏自己的渲染分辨率，由无边框窗口把它拉伸铺满桌面，视野与 2D 排版和原始设计完全一致。
- **真三国无双6 with 猛将传 DX**：第三方 `resolution_for_SM6TC` 与本补丁冲突 —— 它把引擎内部渲染分辨率改成 2560×1440，但游戏的逻辑分辨率仍是 1920×1080，两者错位就是「视野异常」，只有在游戏内重新应用一次分辨率才会重新同步。`Fix_for_SM6`（修复面部表情）不受影响，保持可用。
- 新增 `[platform] frame_capture` 诊断开关：置 1 后在游戏目录创建 `MUSOUTweaksPC_capture.flag`，下一帧会从后备缓冲直接写出 `MUSOUTweaksPC_frame.bmp`。可绕开桌面截屏拿不到游戏画面的问题。

### 2.44
- **真三国无双6 with 猛将传 DX**：修复开启 `native_resolution` 后画面卡死（按键有声音、画面不刷新）。根因是引擎按自己请求的分辨率创建深度模板缓冲；后备缓冲区放大后视口超出该表面，D3D9 会拒绝所有 `Clear` 和带深度测试的绘制，画面停在旧帧。现在深度模板缓冲会同步放大到后备缓冲区尺寸，**无边框缩放与高分辨率 MOD 可同时使用**。
- **真三国无双6 with 猛将传 DX**：`dpad_as_left_stick` 改为默认开启。该作找不到游戏原生菜单映射表，菜单只认左摇杆，十字键必须桥接到左摇杆才能移动菜单光标。

### 2.43
- 新增真三国无双7 with 猛将传、无双蛇魔 Z 的视野调整组合键。
- 日志收集默认全关；战国无双2 帧率限制默认 65。

## 版本

当前构建 **2.45**，对外发布版本 **V1.0.1**。发布包名称、大小与 SHA-256 见 `RELEASES.md`。

---

<a id="english"></a>
# English

**English** | [中文](#中文)

32-bit `dinput8.dll` proxy patch that adds modern gamepad support, windowed-input compatibility and resolution scaling to Koei's early PC ports of the *Musou* series. One DLL picks the right INI by executable name, so dropping it into the game folder is all it takes — no registry writes and no global hooks.

## Supported games

| Game | Executable | Config | What the patch does |
| --- | --- | --- | --- |
| Dynasty Warriors 4: Hyper (SSM3) | `ShinSangokumusou3.exe` | `SSM3_Tweaks.ini` | Gamepad adapter, D-pad, borderless window, IME isolation |
| Dynasty Warriors 5 Special (SSM4SP) | `SM4_WIN.exe` / `SS4*.exe` | `SSM4_Tweaks.ini` | Gamepad adapter, D-pad menu navigation, borderless window |
| Dynasty Warriors 6 with Xtreme Legends DX (SSM6DX) | `SM6TC.exe` / `SM6EN.exe` / `SM6JP.exe` | `SSM6_Tweaks.ini` | Gamepad adapter, D-pad menu navigation, borderless scaling, background-scanner crash fix |
| Dynasty Warriors 8 with Xtreme Legends (SSM7DX) | `Launch.exe` | `SSM7_Tweaks.ini` | Borderless fullscreen, sharp scaling, internal render resolution, 3D camera distance |
| Samurai Warriors 2 (SM2) | `SW2.exe` | `SM2_Tweaks.ini` | Gamepad adapter, borderless window, engine resolution port, HUD fix, frame limiter |
| Musou Orochi Z (MOZ) | `OROCHI_Z_TC.exe` | `OrochiZPadX.ini` | Gamepad adapter, borderless window, 3840x2160 internal rendering, camera distance |

## What each patch does

### Dynasty Warriors 4: Hyper (SSM3)

Turns XInput and native Sony DS4 pads into the virtual DirectInput device the game expects, with hot-plug and foreground-only delivery. The D-pad is exposed as four independent buttons (13-16) plus a POV hat and is never merged into the left-stick axes, so tapping it cannot walk the character. On the window side: borderless fullscreen, in-game IME isolation and a bypass for the close-confirmation dialog.

**The game keeps its own 1280x960 render target** and is only upscaled by the borderless window; every high-resolution override is refused. This build's engine canvas and UI are hardcoded to that size, and overriding just the D3D backbuffer crops the scene.

### Dynasty Warriors 5 Special (SSM4SP)

Same gamepad and window handling as SSM3, plus a signature-verified native menu map, so the D-pad moves the cursor in game menus. Resolution policy is identical: original render target only, high-resolution overrides refused.

### Dynasty Warriors 6 with Xtreme Legends DX (SSM6DX)

Gamepad adapter, D-pad menu navigation, borderless fullscreen, single primary-chain Present (so Steam/RTSS counts one frame and overlays stay attached); the borderless window stretches the game's own frame to the desktop. Also fixes a crash in the game's background scanner thread. The Traditional Chinese build (`SM6TC.exe`) can additionally load the bundled third-party mod: a `d3dx9_43.dll` loader plus `Fix_for_SM6.dll` (facial expression fix), installed with a timestamped backup by `install-ssm6dx.ps1`; see `THIRD_PARTY_MODS.md`.

### Dynasty Warriors 8 with Xtreme Legends (SSM7DX)

This title uses the platform-only build, `dinput8-platform.dll` (the game already speaks XInput). It provides borderless fullscreen, single Present and Catmull-Rom sharp scaling, an optional internal render resolution (3840x2160 by default via `internal_render_width/height`, still presented at desktop size), an adjustable 3D camera distance (`camera_distance_percent`, 50-300, 100 = original) and **camera hotkey shortcuts**.

### Samurai Warriors 2 (SM2)

Gamepad adapter, D-pad, borderless window and close-confirmation bypass. A separate engine-resolution port waits for the protected executable to decrypt, then replaces its width/height getters in memory and corrects HUD/menu geometry; `0/0` follows the active display's physical resolution. The frame limiter defaults to **65 FPS** (`fps_limit=65`) and never touches the Windows desktop mode or refresh rate.

### Musou Orochi Z (MOZ)

Gamepad adapter (including native DS4), borderless window and **3840x2160 internal engine rendering** presented through the game's primary swapchain so video backgrounds survive. Two rendering issues that come with the higher resolution are corrected: GS sprite Y-coordinate overflow above 2048 pixels, and full-screen backgrounds filling the depth buffer so later 3D maps, loading and result screens get occluded. Also supports the 3D camera distance and **camera hotkey shortcuts**.

## Camera hotkeys

Orochi Z and DW8 can change the 3D camera distance live. Defaults:

| Shortcut | Effect |
| --- | --- |
| `Ctrl + Alt + PageUp` | Move the camera farther (+5% per press, up to 300%) |
| `Ctrl + Alt + PageDown` | Move the camera nearer (-5% per press, down to 50%) |
| `Ctrl + Alt + Home` | Reset to the original 100% |

Only the viewpoint moves: FOV, character proportions and HUD scale are preserved, and the shortcuts respond only while the game is in the foreground. Adjustments are saved back to `camera_distance_percent` in the INI. Keys, step size and modifiers are configurable (`camera_hotkey_farther/nearer/reset`, `camera_distance_step`, `camera_hotkey_modifiers`); changes apply within about 500 ms once you move the character or camera. Preparation maps in Orochi Z keep their original framing.

## Installation

1. Grab `MUSOUTweaksPC_V<version>.zip` from [Releases](../../releases) and extract it. The bundle is split per game — unpack only the sub-zip you need (each holds `dinput8.dll` plus its INI).
2. Copy the right DLL and its INI into the folder that holds the game executable:

| Game | Target folder | DLL |
| --- | --- | --- |
| DW4 Hyper | `Shin Sangokumusou 3\game` | `dinput8.dll` |
| DW5 Special | `Shin Sangokumusou 4` | `dinput8.dll` |
| DW6 XL DX | `Dynasty Warriors 7 DX` | `dinput8.dll` |
| DW8 XL | `Dynasty Warriors 8` | `dinput8-platform.dll` (rename to `dinput8.dll`) |
| Samurai Warriors 2 | game root | `dinput8.dll` |
| Musou Orochi Z | game root | `dinput8.dll` |

3. For DW6 XL DX you can run `install-ssm6dx.ps1`, which makes a timestamped backup and installs the third-party mods too; `restore.ps1` undoes it.
4. To uninstall, delete the `dinput8.dll` and INI you copied in.

## Configuration

Every INI line carries **both Chinese and English comments**; key names stay in English. Restart the game after editing. Missing keys fall back to defaults and out-of-range values are clamped.

- **Logging is off by default**: `log=0`. Set `log=1` only when you need `MUSOUTweaksPC.log` for troubleshooting.
- `[padfix]`: `enable` master switch, `backend` auto/XInput/native DS4, `dpad_*` D-pad behaviour, plus deadzone, trigger threshold and connection guard.
- `[platform]`: `window_mode`, `single_present`, `disable_game_ime`, `com_apartment_compat`, `close_without_confirmation`, `fps_limit`.
- Resolution keys differ per game: DW4/DW5 refuse overrides; DW6 DX, DW8, Orochi Z and SW2 each use a verified engine path.

See `CONFIGURATION.md` for every key and `MULTI_GAME.md` for per-title differences.

## Building from source

Requires the Visual Studio x86 C++ toolchain:

```bat
build.cmd           :: build\dinput8.dll (with gamepad adapter)
build-platform.cmd  :: build\dinput8-platform.dll (platform hooks only)
python package.py   :: package both DLLs
```

## Third-party components

Sources and licenses for the DW6 XL DX Traditional Chinese mods are documented in `THIRD_PARTY_MODS.md`.

## Changelog

### 2.45
- **SSM6DX**: the D-pad now navigates menus through the game's own keyboard device, and `dpad_as_left_stick` is back to 0. Diagnostics showed this build reads DirectInput keyboards in **buffered** mode (`GetDeviceData`, capacity 64) rather than polling `GetDeviceState`, so the new `dpad_arrow_keys` option appends paired `DIK_UP/DOWN/LEFT/RIGHT` key-down and key-up events to that device. The menu cursor moves, the left stick stays untouched, and **the character no longer walks**.
- **SSM6DX**: `native_resolution` / `scale_to_fill` now default to 0. With native rendering the game only draws into the top-left 1920x1080 area of a 2560x1440 backbuffer, leaving black bars on the right and bottom; enabling `scale_to_fill` instead mismatches the 3D viewport against a 2D layer laid out for 1920x1080, which shows up as the wrong field of view. The game now keeps its own render resolution and the borderless window stretches that image to the desktop, so the view and the 2D layout match the original design.
- **SSM6DX**: the third-party `resolution_for_SM6TC` mod conflicts with this patch. It forces the engine's internal render size to 2560x1440 while the game's logical resolution stays 1920x1080; that mismatch is the wrong field of view, and it only re-syncs when the resolution is applied again in game. `Fix_for_SM6` (facial expression fix) is unaffected and stays supported.
- New `[platform] frame_capture` diagnostic: set it to 1, then create `MUSOUTweaksPC_capture.flag` in the game folder and the next frame is written straight from the backbuffer to `MUSOUTweaksPC_frame.bmp`. Useful when a desktop capture cannot see the game window.

### 2.44
- **SSM6DX (Dynasty Warriors 7 DX)**: fixed the frozen picture with `native_resolution` enabled (input and audio kept working while the screen stopped updating). The engine creates its depth-stencil surface at the resolution it requested; once the backbuffer is enlarged the viewport no longer fits that surface, so D3D9 rejects every `Clear` and every depth-tested draw and the frame stays stale. The depth-stencil surface is now grown to the backbuffer size, so **borderless scaling and external high-resolution mods can be used together**.
- **SSM6DX**: `dpad_as_left_stick` now defaults to on. No native menu map can be located for this title and its menus only read the left stick, so the D-pad has to be bridged to move the menu cursor.

### 2.43
- Added the camera-distance hotkeys for SSM7 and Musou Orochi Z.
- Log collection is off by default; the Sengoku Musou 2 frame limiter defaults to 65.

## Version

Current build: **2.45**; public release: **V1.0.1**. Package names, sizes and SHA-256 are listed in `RELEASES.md`.
