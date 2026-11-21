# 老无双系列 PC 修复补丁 / MUSOUTweaksPC

<a id="中文"></a>
**中文** | [English](#english)

32 位 `dinput8.dll` 代理补丁，为光荣「无双」系列早期 PC 移植版补上现代手柄支持、窗口输入兼容和分辨率扩展。同一个 DLL 按游戏可执行文件名自动选择对应的 INI 配置，放进游戏目录即可生效，不改注册表、不写系统全局钩子。

## 支持的游戏

| 游戏 | 适配版本 | 可执行文件 | 配置文件 | 补丁作用 |
| --- | --- | --- | --- | --- |
| 真三国无双3(SSM3_Shin Sangokumusou3) | 游侠繁体汉化 `V0.1.1.0`（`2.0.0.0`） | `ShinSangokumusou3.exe` | `SSM3_Tweaks.ini` | 手柄适配、十字键、无边框窗口、输入法隔离 |
| 真三国无双4 Special(SSM4SP_Shin Sangokumusou4 Special) | `V1.0.0.0` | `SM4_WIN.exe` / `SS4*.exe` | `SSM4_Tweaks.ini` | 手柄适配、十字键菜单导航、无边框窗口 |
| 真三国无双5(SSM5_Shin Sangokumusou5) | `V1.0.0.1` | `SM5_WIN.exe` | `SSM5_Tweaks.ini` | 手柄适配（按游戏名单报告设备名）、右摇杆转视角、十字键、无边框全屏 |
| 真三国无双6with猛将传DX(SSM6DX_Shin Sangokumusou6 with Moushouden DX) | Steam `V1.0.0.0` | `SM6TC.exe` / `SM6EN.exe` / `SM6JP.exe` | `SSM6_Tweaks.ini` | 手柄适配、十字键菜单导航、无边框全屏缩放、后台扫描器崩溃修复 |
| 真三国无双7with猛将传(SSM7DX_Shin Sangokumusou7 with Moushouden) | Steam `V1.0.4.0` | `Launch.exe` | `SSM7_Tweaks.ini` | 无边框全屏、清晰缩放、内部渲染分辨率、3D 镜头远近 |
| 战国无双2(SM2_SengokuMusou2) | `V1.0.0.0` | `SW2.exe` | `SM2_Tweaks.ini` | 手柄适配、无边框窗口、引擎分辨率移植、HUD 校正、帧率限制 |
| 无双蛇魔Z(MOZ_MusouOrochi Z) | `V1.0.0.0` | `OROCHI_Z_TC.exe` | `OrochiZPadX.ini` | 手柄适配、无边框窗口、3840×2160 独立渲染、镜头远近 |

「适配版本」是补丁实际验证过的游戏版本。补丁按可执行文件名和内存特征码识别，其他版本大概率也能用；如果换了版本后某个功能失效，把 ini 里的 `log` 改成 `1` 跑一次，日志会写明哪一步没匹配上。

## 各补丁具体做什么

### 真三国无双3(SSM3_Shin Sangokumusou3)

XInput 与原生 Sony DS4 手柄转成游戏能识别的虚拟 DirectInput 手柄，支持热插拔和「仅前台生效」；十字键输出为四个独立按键（Button 13–16）并另提供 POV 方向帽，不合并到左摇杆，避免推十字键时角色乱跑。窗口侧提供无边框全屏、游戏内输入法隔离、跳过关闭确认框。

**分辨率保持游戏自身 1280×960**，只由无边框窗口放大，任何高分辨率覆写都会被拒绝——这一作的引擎画布和 UI 是按固定尺寸写死的，仅改 D3D 后备缓冲区会裁切画面。

**小地图放大**：这一作的小地图只有 104×128 像素（分屏时每个视口各一块），在放大到桌面尺寸的窗口里显得很小。补丁在 D3D9 里把**每帧采样 410×440 地图图集的最大那个屏幕空间四边形**认作底图——不按尺寸写死，单人与分屏画法尺寸不同都能认；复制它的顶点、以四边形右上角为锚点等比放大后提交副本，默认 **160%**（`minimap_scale`）。地图上的标记点（单位点、玩家箭头、据点图标）是各自单独提交的小四边形，补丁把它们匹配到所在视口的地图矩形、用同一套变换一起放大，底图与标记点始终同步。游戏自己的顶点缓冲不改，同一帧其他 HUD 不受影响。运行中可用 **`Ctrl+Alt+F2` 放大、`Ctrl+Alt+F3` 缩小**（每次 10%，范围 80–320%，按住可连调），新值立即写回 ini。

### 真三国无双4 Special(SSM4SP_Shin Sangokumusou4 Special)

与真三3 同一套手柄与窗口处理，另外通过代码指纹校验定位了本作的原生菜单映射表，因此十字键可以在游戏菜单里移动光标。分辨率策略与真三3 相同：保持游戏自身渲染目标，拒绝高分辨率覆写。

### 真三国无双5(SSM5_Shin Sangokumusou5)

这一作的手柄列表是**按名字过滤**的：`SM5_WIN.exe` 里带一份 DirectInput 产品名白名单（Xbox 360 手柄、罗技 ChillStream、ELECOM/SANWA 等），名字不在名单里的设备能被枚举到但永远不会被选中，表现成手柄完全没反应。所以虚拟手柄默认报成 `Controller (XBOX 360 For Windows)`（`pad_name`）。本作用右摇杆转视角，虚拟设备默认暴露 **6 轴**（左摇杆 + 扳机 + 右摇杆，`pad_axes=6`），而不是其他作品的两轴。**十字键只作为 POV 方向帽和四个独立按钮交给游戏**（`dpad_as_left_stick=0`），可以单独绑键，也不会带动角色；菜单光标走游戏自己的键盘设备（`dpad_arrow_keys=1`，该作以缓冲模式读键盘），按住超过 0.4 秒后自动连发。窗口侧是无边框全屏 + 输入法隔离 + 跳过关闭确认框，画面按游戏自己选的分辨率渲染后由窗口拉伸铺满。

### 真三国无双6with猛将传DX(SSM6DX_Shin Sangokumusou6 with Moushouden DX)

手柄适配 + 十字键菜单导航 + 无边框全屏 + 单次主交换链呈现（让 Steam/RTSS 按一帧统计，覆盖层不掉帧计数），画面由无边框窗口拉伸铺满桌面。修复了本作后台扫描线程在特定时序下的崩溃。繁体中文版（`SM6TC.exe`）可额外安装附带的第三方 MOD：`d3dx9_43.dll` 加载器 + `Fix_for_SM6.dll`（修复面部表情），由 `install-ssm6dx.ps1` 自动备份后安装，细节见 `THIRD_PARTY_MODS.md`。

### 真三国无双7with猛将传(SSM7DX_Shin Sangokumusou7 with Moushouden)

这一作使用平台专用构建 `dinput8-platform.dll`（游戏本身已支持 XInput）。提供无边框全屏、单次 Present、Catmull-Rom 清晰缩放；可选内部渲染分辨率（默认 3840×2160，由 `internal_render_width/height` 控制，输出仍是桌面尺寸）；3D 镜头远近可调（`camera_distance_percent`，50–300，100 为原始），并支持**视野调整组合键**。

### 战国无双2(SM2_SengokuMusou2)

手柄适配 + 十字键 + 无边框窗口 + 跳过关闭确认框。十字键只作为 POV 方向帽和四个独立按钮交给游戏（`dpad_pov=1`、`dpad_as_left_stick=0`），可以单独绑键、也不会带动角色；菜单光标走键盘方向键（`dpad_arrow_keys=2`，`SendInput` 注入）。这一作的菜单光标和角色移动读的是**同一路键盘输入**，所以补丁在 D3D9 里统计每帧的真实 3D 绘制次数，只在菜单/读盘/结算画面（`menu_inject_only=1`）注入方向键，战斗场景完全不注入——菜单能动、角色不会跟着走。另有一条独立的引擎分辨率移植路径：等待受保护的可执行文件解密后，在内存中替换宽高查询函数并校正 HUD/菜单几何，`0/0` 表示跟随当前显示器物理分辨率。帧率限制默认 **60 FPS**（`fps_limit` 在程序内对 `SW2.exe` 就取 60）：这一作逻辑每渲染一帧推进一次，帧率就是游戏速度，65 帧会整体快 8.3%。不修改 Windows 桌面分辨率或刷新率。

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
| 真三5 | `Shin Sangokumusou 5` | `dinput8.dll` |
| 真三6 DX | `Dynasty Warriors 7 DX` | `dinput8.dll` |
| 真三7 | `Dynasty Warriors 8` | `dinput8-platform.dll`（改名 `dinput8.dll`） |
| 战国无双2 | 游戏根目录 | `dinput8.dll` |
| 无双蛇魔Z | 游戏根目录 | `dinput8.dll` |

3. 真三6 DX 可直接运行 `install-ssm6dx.ps1`，它会做时间戳备份并顺带装第三方 MOD；恢复用 `restore.ps1`。
4. 想撤销：删掉放进游戏目录的 `dinput8.dll` 与 INI 即可，游戏恢复原状。

## 配置

所有新增 INI 配置项都有中文和英文注释，键名保留英文。修改后需完全退出并重新启动游戏。缺项自动取默认值，超范围数值会被夹到有效区间。

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

### V1.0.5

本版整理为正式发布包。默认 `controller_takeover=1` 时，1P手柄接管公共菜单确认/返回，2P只使用自己的键盘或第二手柄；将其设为 `0` 会完整恢复游戏原生的1P键盘、2P手柄和右侧键盘分配，不安装新增的双人菜单接管路由，也不过滤公共菜单按键。

This release is the cleaned V1.0.5 bundle. With `controller_takeover=1`, the first player's pad owns global menu confirm/back and player two keeps a separate keyboard or controller source. Setting it to `0` restores the game's original keyboard/controller ownership and disables the added co-op menu routing and filtering.

- 修复真三国无双3/4/5及其他合作菜单的1P/2P输入归属、确认返回和子菜单通道。
- 修复 DirectInput 力反馈到 Xbox、DS4、DualSense 双马达的转换，并保留真三7原生输入，只增加震动桥接。
- 修复无边框窗口任务栏激活后的焦点恢复，补充真三6DX视角组合键和真三5扳机/十字键路径。
- 修复真三5短按方向和左摇杆菜单跳格，按住方向只在延迟后连发。
- 配置文件和部署文档补齐中英文说明，发布包只保留最新二进制与配置。

- Fixed first/second-player ownership, confirm/back routing, and child-menu paths across the supported co-op ports.
- Added DirectInput force-feedback conversion for Xbox, DS4, and DualSense motors while keeping DW8's native input path.
- Fixed taskbar focus restoration for borderless windows, added SSM6 DX camera shortcuts, and completed SM5 trigger/D-pad paths.
- Fixed SM5 one-tap D-pad and left-stick menu skipping. Hold-to-repeat starts only after the configured delay.
- Completed bilingual configuration and deployment documentation. The release bundle contains only the latest binaries and configs.

### 2.53-rc2（修复候选）

修正真三3/4菜单接管的输入通道，避免原生A/B已屏蔽、替代Enter/Esc又被键盘过滤吞掉。所有通道都保留手柄按键状态和缓冲事件；新增真实DirectInput键盘集成检查，231项检查通过。保持默认开启1P菜单接管（B确认/A返回）。

### 2.53-rc1（修复候选）

修正方向键短按过早连发、真三5扳机轴/按钮重复输出、无边框激活时被强制压到其他窗口后方的问题；新增 `[input] controller_takeover`、独立2P虚拟设备与DirectInput到XInput的震动转换。真三7保留原生XInput并增加后台震动清理。菜单覆盖、实体震动和远程同乐仍需实机验收，详见 [FIX_STATUS.md](FIX_STATUS.md)。

运行 `test.cmd` 验证输入、窗口和震动接口；`python package-candidate.py` 生成候选包，不覆盖已有发布包。真三4独立无边框包在 `codex/ssm4-borderless-only` 分支单独提供，不包含手柄适配。

### 2.52

- **真三3 小地图：标记点跟随放大**。地图底图是一次绘制，而上面的单位点、玩家箭头、据点图标是各自单独提交的小四边形，之前只放大了底图，标记点全留在原来的小区域里。现在底图定下矩形，任何落在该矩形内的 2D 图元都按同一套变换（锚点仍是右上角）一起放大；分屏时两个视口各自的矩形都会记录，标记点按包含关系匹配到对应视口，不会串到另一个视口。
- **真三3 小地图：单人模式不放大**。上一版用「宽 90–120、高 115–140」的尺寸窗口认定底图，而 104×128 只是**分屏**时每个视口的地图尺寸；单人模式画得更大，直接被窗口排除，于是整段逻辑不生效。现在改为按地图图集纹理识别、取每帧面积最大的那个四边形，不再依赖任何尺寸常量。
- **真三3 小地图：热键此前完全没生效**。热键在 `Present` 里轮询，而这一作四个原有挂载条件（原生分辨率、锁帧、视角调整、截帧）全都不满足，`Present` 从未被接管，轮询一次都没跑过。现在只要 `minimap_hotkeys=1` 就会挂上 `Present`（含晚挂钩路径）。
- 新增诊断日志：`minimap base 宽x高 at ... scaled to N%` 与 `minimap candidate rejected 宽x高 ... surface=纹宽x纹高`，换作或换语言版本时可以直接从日志读出地图的实际尺寸与纹理尺寸。

### 2.51

- **真三3 小地图放大**。这一作的小地图是一张 104×128 的屏幕空间四边形（采样 410×440 的地图图集），补丁按这个指纹认出它，复制顶点并以右上角为锚点等比放大后提交副本，默认 **160%**。游戏自己的顶点缓冲不动，其他 HUD 不受影响，上下分屏时两个视口各自生效。
- 新增运行中热键：**`Ctrl+Alt+F2` 放大 / `Ctrl+Alt+F3` 缩小**，每次 10%，范围 80–320%，按住可连调，结果自动写回 `minimap_scale`。
- 新增配置项 `minimap_scale`、`minimap_hotkeys`、`minimap_surface_width` / `minimap_surface_height`（后两个是小地图纹理尺寸指纹，换语言版本时可调）。
- 新增 D3D9 层的 2D 图元探针 `quad_probe`：把每帧提交的屏幕空间四边形（矩形、FVF、纹理、混合状态）和每张纹理的尺寸写进日志，用于定位 HUD 元素。

### 2.48
- **战国无双2：修复十字键不生效**。这一作的十字键自 2.35 起只作为 POV 方向帽和四个独立按钮转发（`dpad_pov=1`、`dpad_as_left_stick=0`），但它的菜单既不认按钮也不认 POV —— 早先能用是因为 `SM2_Tweaks.ini` 里写的是 `dpad_as_left_stick=1`（把方向桥接进左摇杆轴），桥接一关就彻底没反应。诊断确认 `SW2.exe` 从 `USER32` 只导入了 `MessageBoxA`：既不调 `GetKeyboardState`/`GetAsyncKeyState`，也从不 `GetDeviceState`/`GetDeviceData` 它自己创建的那个键盘设备（日志里只有 `CreateDevice real keyboard=1`，没有任何键盘读取），所以「在游戏键盘设备上塞键」这条老路对它无效。
- 新增 `dpad_arrow_keys=2` / `3`：`2` 用 `SendInput` 把方向键注入到与物理按键完全相同的一层（DirectInput 键盘设备和窗口消息循环都能看到）；`3` 只向游戏窗口投递 `WM_KEYDOWN`/`WM_KEYUP`，DirectInput 设备完全看不到。战国2 自动取 `2`，真三6 DX / 真三5 仍是 `1`。合成按键不会自动重复，所以按住方向时补丁把它切成一次次短按（新配置项 `dpad_arrow_pulse_ms`，默认 `40`；`dpad_arrow_gap_ms`，默认 `80`，合起来约每秒 8 格）。左摇杆完全不动，十字键仍可单独绑键。
- 新增 `menu_inject_only` / `menu_draw_threshold`：**只在菜单帧注入方向键**。战国2 的菜单光标与角色移动共用同一路键盘输入，换注入通道分不开，所以在 D3D9 里统计每帧提交的真实 3D 绘制（FVF 位置类型不是 `XYZRHW` 的 `DrawPrimitive`/`DrawIndexedPrimitive` 及其 `UP` 版本），低于阈值即视为菜单帧、连续 2 帧才切换。战斗场景里完全不注入，十字键不会带动角色。阈值默认 `300`，依据实测：菜单与读盘 `0`–`1`，战斗中暂停菜单（带 3D 地图模型）`54`，真正的战斗场景 `1524`–`2018`。日志会打印 `scene 3d draws=N` 与 `menu scene entered/left` 供校准。
- 战国2 帧率上限的程序内默认值改为 **60**（`fps_limit`），ini 里没有这一项也生效。这一作**逻辑每渲染一帧推进一次，帧率就是游戏速度**：之前按 65 锁帧，实测 `pace target=65 actual=64.99` 帧率完全准确，但整体仍快 8.3%——那不是锁帧失效，而是 65 本身就比原作的 60 帧快。
- **战国2：修复锁帧完全没生效**。这一作的 D3D9 设备从来就没被挂上钩子——它在 `dinput8.dll` 加载之前就已经解析好 `Direct3DCreate9`，早期 IAT 钩子永远不触发；唯一的退路「晚挂钩」线程又因为 `gameoverlayrenderer.dll` 存在就整段放弃，于是 `Present` 没被接管，`fps_limit` 一次都没跑过（实测 165 FPS，等于显示器刷新率）。现在晚挂钩不再因为覆盖层存在就直接退出：先给早期钩子一段宽限期，仍未接管才回退到堆扫描，并且只挂第一张匹配的设备 vtable，避免每帧走两次节拍把帧率减半。

### 2.47
- **无双蛇魔 Z：修复高分辨率下整屏图层错乱**。引擎的精灵坐标是 12.4 定点数，内部渲染高度超过 2048 就会溢出，补丁被迫启用「精灵 Y 溢出修补」和「背景四边形深度写入 hack」两套补丁；两者叠加后，铺满后备缓冲的背景四边形会按 `D3DCMP_LESSEQUAL` 把后面所有 `z>0` 的层判死，表现为：选择故事/列传多出不该有的背景层、兵营界面整屏发白多层叠加、loading 只剩余底色、即时演算字幕消失。现在 `internal_render_width` / `internal_render_height` 默认跟随桌面分辨率（`0`/`0`），配置说明里写明 **2048 是红线**，超过即触发上述修补。
- 新增 `orochi_depth_fix`：背景四边形深度写入的处理方式，`0` 完全不改、`1` 每帧所有全屏四边形都禁止写入（旧行为）、`2` 只禁止每帧第一个（默认）。仅在内部渲染高度 > 2048 时才生效。
- **无双蛇魔 Z：手柄 B 键 / OPTION 键等同键盘 Enter**。选择故事、列传等界面以及「2P 加入」弹窗只接受键盘确认，手柄原来完全无响应。新增 `pad_enter_buttons`（默认 `0x82` = B + OPTION）、`pad_enter_mode`（默认 `3` = `SendInput`）、`pad_enter_pulse_ms`（默认 `60`）。该作不读 DirectInput 键盘设备、也不认 `PostMessage` 投递的窗口消息，只有 `SendInput` 走系统输入栈生成的 `WM_KEYDOWN`/`WM_CHAR`/`WM_KEYUP` 才能被识别。

### 2.46
- **新增真三国无双5（`SM5_WIN.exe`）支持**，配置文件 `SSM5_Tweaks.ini`。该作的手柄列表按**产品名白名单**过滤，名单外的设备能被枚举但不会被选中，所以虚拟手柄默认报成 `Controller (XBOX 360 For Windows)`（新配置项 `pad_name`）。
- 虚拟 DirectInput 设备新增 `pad_axes`：`2` 维持原来的「只有左摇杆」，`6` 额外暴露 Z（左扳机）、Rx/Ry（右摇杆）、Rz（右扳机）。真三5 默认 `6`，因为该作用右摇杆转视角。其他作品默认仍是 `2`，行为不变。
- 真三5 十字键默认 `dpad_pov=1` + `dpad_as_left_stick=0` + `dpad_arrow_keys=1`：十字键只作为 POV 方向帽和独立按钮交给游戏（可以单独绑键、不会带动角色），菜单光标走游戏自己的键盘设备（该作以缓冲模式读键盘，`GetDeviceData` 容量 10）。主窗口按注册类名 `SangokuMusou5PC` 识别。
- 新增 `dpad_menu_repeat_ms` / `dpad_menu_repeat_delay_ms`：菜单「按一次走一格」的作品，按住方向超过延迟时间后会周期性松开一小段，游戏重新收到按下事件，光标持续移动。连发只作用于 POV 方向帽、十字键按钮和方向键注入，`dpad_as_left_stick=0` 时左摇杆完全不动。真三5 默认开启（120/400），其余作品默认 `0`（关闭），行为不变。
- 无边框窗口新增**按类名轮询**兜底：有些作品（真三5 的 `SM5_WIN.exe` 只静态导入 `MessageBoxA`）根本不走 `CreateWindowExA` 的 IAT 槽，原来的窗口钩子永远打不上，无边框缩放静默失效。现在若窗口钩子缺失，补丁会轮询本进程的顶层窗口找注册类名，并在日志里列出所有候选类名；找不到预期类名时，只要进程只有一个大窗口也会接管。真三5 实测日志：`main window watcher started` → `main window hooked class=SangokuMusou5PC bounds=0,0 2560x1440`。
- 新增 `install-ssm5.ps1`；`install-targets.ps1`、`restore.ps1` 增加真三5 条目。

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

当前构建 **2.53**，对外发布版本 **V1.0.5**。发布包名称、大小与 SHA-256 见 `RELEASES.md`。

---

<a id="english"></a>
# English

**English** | [中文](#中文)

32-bit `dinput8.dll` proxy patch that adds modern gamepad support, windowed-input compatibility and resolution scaling to Koei's early PC ports of the *Musou* series. One DLL picks the right INI by executable name, so dropping it into the game folder is all it takes — no registry writes and no global hooks.

## Supported games

| Game | Tested version | Executable | Config | What the patch does |
| --- | --- | --- | --- | --- |
| Dynasty Warriors 4: Hyper (SSM3) | Ali213/Traditional-Chinese fan patch `V0.1.1.0` (`2.0.0.0`) | `ShinSangokumusou3.exe` | `SSM3_Tweaks.ini` | Gamepad adapter, D-pad, borderless window, IME isolation |
| Dynasty Warriors 5 Special (SSM4SP) | `V1.0.0.0` | `SM4_WIN.exe` / `SS4*.exe` | `SSM4_Tweaks.ini` | Gamepad adapter, D-pad menu navigation, borderless window |
| Dynasty Warriors 6 (SSM5) | `V1.0.0.1` | `SM5_WIN.exe` | `SSM5_Tweaks.ini` | Gamepad adapter with a whitelisted device name, right-stick camera, D-pad, borderless fullscreen |
| Dynasty Warriors 7 with Xtreme Legends DX (SSM6DX) | Steam `V1.0.0.0` | `SM6TC.exe` / `SM6EN.exe` / `SM6JP.exe` | `SSM6_Tweaks.ini` | Gamepad adapter, D-pad menu navigation, borderless scaling, background-scanner crash fix |
| Dynasty Warriors 8 with Xtreme Legends (SSM7DX) | Steam `V1.0.4.0` | `Launch.exe` | `SSM7_Tweaks.ini` | Borderless fullscreen, sharp scaling, internal render resolution, 3D camera distance |
| Samurai Warriors 2 (SM2) | `V1.0.0.0` | `SW2.exe` | `SM2_Tweaks.ini` | Gamepad adapter, borderless window, engine resolution port, HUD fix, frame limiter |
| Musou Orochi Z (MOZ) | `V1.0.0.0` | `OROCHI_Z_TC.exe` | `OrochiZPadX.ini` | Gamepad adapter, borderless window, 3840x2160 internal rendering, camera distance |

"Tested version" is the build the patch was verified against. It identifies games by executable name and in-memory signatures, so other builds will most likely work too; if something stops working after an update, set `log=1` in the INI and the log will say which step failed to match.

## What each patch does

### Dynasty Warriors 4: Hyper (SSM3)

Turns XInput and native Sony DS4 pads into the virtual DirectInput device the game expects, with hot-plug and foreground-only delivery. The D-pad is exposed as four independent buttons (13-16) plus a POV hat and is never merged into the left-stick axes, so tapping it cannot walk the character. On the window side: borderless fullscreen, in-game IME isolation and a bypass for the close-confirmation dialog.

**The game keeps its own 1280x960 render target** and is only upscaled by the borderless window; every high-resolution override is refused. This build's engine canvas and UI are hardcoded to that size, and overriding just the D3D backbuffer crops the scene.

### Dynasty Warriors 5 Special (SSM4SP)

Same gamepad and window handling as SSM3, plus a signature-verified native menu map, so the D-pad moves the cursor in game menus. Resolution policy is identical: original render target only, high-resolution overrides refused.

### Dynasty Warriors 6 (SSM5)

This port filters controllers **by product name**: `SM5_WIN.exe` carries a whitelist of DirectInput names (Xbox 360 pad, Logitech ChillStream, ELECOM/SANWA pads, ...) and silently ignores anything else, so a device with an unknown name is enumerated but never selected and the pad looks dead. The virtual device therefore reports itself as `Controller (XBOX 360 For Windows)` by default (`pad_name`). The game steers the camera with the right stick, so the device exposes **six axes** (`pad_axes=6`: left stick, triggers, right stick) instead of the two the rest of the family uses. **The D-pad is only a POV hat plus four independent buttons** (`dpad_as_left_stick=0`): it keeps its own bindings and never walks the character, while menu navigation goes through the game's own keyboard device (`dpad_arrow_keys=1`, this build reads the keyboard in buffered mode) and repeats automatically after a 0.4 s hold. On the window side: borderless fullscreen, IME isolation, close-confirmation bypass; the game renders at its own resolution and the window stretches that image.

### Dynasty Warriors 7 with Xtreme Legends DX (SSM6DX)

Gamepad adapter, D-pad menu navigation, borderless fullscreen, single primary-chain Present (so Steam/RTSS counts one frame and overlays stay attached); the borderless window stretches the game's own frame to the desktop. Also fixes a crash in the game's background scanner thread. The Traditional Chinese build (`SM6TC.exe`) can additionally load the bundled third-party mod: a `d3dx9_43.dll` loader plus `Fix_for_SM6.dll` (facial expression fix), installed with a timestamped backup by `install-ssm6dx.ps1`; see `THIRD_PARTY_MODS.md`.

### Dynasty Warriors 8 with Xtreme Legends (SSM7DX)

This title uses the platform-only build, `dinput8-platform.dll` (the game already speaks XInput). It provides borderless fullscreen, single Present and Catmull-Rom sharp scaling, an optional internal render resolution (3840x2160 by default via `internal_render_width/height`, still presented at desktop size), an adjustable 3D camera distance (`camera_distance_percent`, 50-300, 100 = original) and **camera hotkey shortcuts**.

### Samurai Warriors 2 (SM2)

Gamepad adapter, D-pad, borderless window and close-confirmation bypass. A separate engine-resolution port waits for the protected executable to decrypt, then replaces its width/height getters in memory and corrects HUD/menu geometry; `0/0` follows the active display's physical resolution. The frame limiter defaults to **60 FPS** (`fps_limit=60`) and never touches the Windows desktop mode or refresh rate.

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
| DW6 | `Shin Sangokumusou 5` | `dinput8.dll` |
| DW7 XL DX | `Dynasty Warriors 7 DX` | `dinput8.dll` |
| DW8 XL | `Dynasty Warriors 8` | `dinput8-platform.dll` (rename to `dinput8.dll`) |
| Samurai Warriors 2 | game root | `dinput8.dll` |
| Musou Orochi Z | game root | `dinput8.dll` |

3. For DW6 XL DX you can run `install-ssm6dx.ps1`, which makes a timestamped backup and installs the third-party mods too; `restore.ps1` undoes it.
4. To uninstall, delete the `dinput8.dll` and INI you copied in.

## Configuration

Every newly added INI option carries **both Chinese and English comments**; key names stay in English. Restart the game after editing. Missing keys fall back to defaults and out-of-range values are clamped.

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

### V1.0.5

This is the cleaned V1.0.5 release bundle. With `controller_takeover=1`, the first player's pad owns global menu confirm/back and player two keeps a separate keyboard or controller source. Setting it to `0` restores the game's original keyboard/controller ownership and disables the added co-op routing and filtering.

- Fixed first/second-player ownership, confirm/back routing, and child-menu paths across the supported co-op ports.
- Added DirectInput force-feedback conversion for Xbox, DS4, and DualSense motors while keeping DW8's native input path.
- Fixed taskbar focus restoration for borderless windows, added SSM6 DX camera shortcuts, and completed SM5 trigger/D-pad paths.
- Fixed SM5 one-tap D-pad and left-stick menu skipping. Hold-to-repeat starts only after the configured delay.
- Completed bilingual configuration and deployment documentation. The release bundle contains only the latest binaries and configs.

### 2.52

- **SSM3 minimap: markers now scale with the map.** The base map is one draw, while the unit blips, the player arrow and the camp icons are each submitted as their own little quad; only the base was scaled before, so every marker stayed behind in the old, smaller footprint. The base quad now fixes a rectangle and any 2D primitive inside it is moved by the same transform (same top-right anchor). Split screen records one rectangle per viewport and matches each marker to the one that contains it, so the two views never cross.
- **SSM3 minimap: single player was not magnified at all.** The previous build identified the base map with a size window of width 90-120 and height 115-140, but 104x128 is the **split-screen** size; single player draws the map larger, fell outside the window, and the whole path went dead. Detection is now by map-atlas texture, taking the largest such quad of the frame, with no size constant anywhere.
- **SSM3 minimap: the hotkeys had never fired.** They are polled from `Present`, and this port satisfied none of the four existing hook conditions (native resolution, frame limit, camera tweak, frame capture), so `Present` was never taken over and the poll never ran once. `minimap_hotkeys=1` now installs the `Present` hook on its own, late-hook path included.
- New diagnostics: `minimap base WxH at ... scaled to N%` and `minimap candidate rejected WxH ... surface=texWxtexH`, so the real map size and atlas size of another port or language build can be read straight from the log.

### 2.51

- **SSM3: minimap magnification.** The minimap is a 104x128 screen-space quad per viewport sampling a 410x440 map atlas. The patch takes **the largest screen-space quad of each frame that samples that atlas** as the base map — no size constant, so single player (drawn larger) and split screen (104x128 per viewport) both work — copies its vertices, scales them around the quad's top-right corner and submits the copy, **160%** by default. The markers on top of the map (unit blips, the player arrow, camp icons) are separate little quads: they are matched to the rectangle of their own viewport and moved by the same transform, so base map and markers always stay in sync. The game's own vertex buffer is never touched and the rest of the HUD is unaffected.
- New in-game hotkeys: **`Ctrl+Alt+F2` grows / `Ctrl+Alt+F3` shrinks** the minimap in 10% steps, 80-320%, hold to repeat; the value is saved back to `minimap_scale`.
- New options `minimap_scale`, `minimap_hotkeys`, `minimap_surface_width` / `minimap_surface_height` (the last two are the texture-size fingerprint, tunable for other language builds).
- Adds a D3D9 2D-quad probe (`quad_probe`) that logs every screen-space quad (rect, FVF, texture, blend state) and every texture size, used to locate HUD elements.

### 2.48
- **Sengoku Musou 2: fixed the dead D-pad.** Since 2.35 the D-pad is only forwarded as a POV hat plus four independent buttons (`dpad_pov=1`, `dpad_as_left_stick=0`), but this title's menus read neither. It used to work only because `SM2_Tweaks.ini` carried `dpad_as_left_stick=1` (bridging the directions into the left-stick axes); turning the bridge off left the D-pad with no path at all. Diagnostics confirmed `SW2.exe` imports nothing from `USER32` but `MessageBoxA`: it never calls `GetKeyboardState`/`GetAsyncKeyState`, and it never calls `GetDeviceState`/`GetDeviceData` on the keyboard device it creates (the log shows `CreateDevice real keyboard=1` and no keyboard read at all), so pressing keys on the game's own keyboard device cannot work for it.
- New `dpad_arrow_keys=2` / `3`: `2` injects the arrows with `SendInput`, i.e. at exactly the layer a physical key press arrives at, so both the DirectInput keyboard device and the window message loop see them; `3` only posts `WM_KEYDOWN`/`WM_KEYUP` to the game window, leaving the DirectInput device untouched. Sengoku Musou 2 selects `2` automatically; SSM6 DX and DW6 keep `1`. A synthesised key never auto-repeats, so a held direction is chopped into short presses (`dpad_arrow_pulse_ms`, default `40`) separated by gaps (`dpad_arrow_gap_ms`, default `80`) — about eight menu steps per second. The left stick never moves and the D-pad keeps its own bindings.
- New `menu_inject_only` / `menu_draw_threshold`: **arrows are injected only on menu frames**. Sengoku Musou 2 feeds its menu cursor and its character movement from one and the same keyboard path, so no injection channel can tell them apart; the D3D9 layer therefore counts the real 3D draws a frame submits (position type other than `XYZRHW`, across `DrawPrimitive`/`DrawIndexedPrimitive` and their `UP` variants) and treats a frame below the threshold as a menu, switching state only after two consecutive frames. Battles get no arrows at all, so the D-pad can never walk the character. The threshold defaults to `300`, measured on this port: menus and loading `0`-`1`, the in-battle pause menu with its 3D map model `54`, real battles `1524`-`2018`. The log prints `scene 3d draws=N` and `menu scene entered/left` for calibration.
- Sengoku Musou 2 now defaults to a **60 FPS** cap in code (`fps_limit`), even when the ini does not carry the key. This port advances its logic once per rendered frame, so **the frame rate is the game speed**: the previous 65 FPS cap measured exactly right (`pace target=65 actual=64.99`) and still played 8.3% fast, because 65 is simply faster than the 60 the port was built for.
- **Sengoku Musou 2: the frame limiter never ran at all.** Its D3D9 device was never hooked: the build resolves `Direct3DCreate9` before `dinput8.dll` is loaded, so the early IAT hook never fires, and the fallback "late" watcher bailed out entirely whenever `gameoverlayrenderer.dll` was present — which left `Present` unhooked and `fps_limit` unused (measured 165 FPS, i.e. the display refresh). The watcher now gives the early hook a grace period and only then falls back to the heap scan, and it hooks a single matching device vtable so pacing cannot run twice per frame and halve the frame rate.

### 2.47
- **Musou Orochi Z: fixed the whole-screen layering break at high resolutions.** The engine stores sprite coordinates as 12.4 fixed point, so an internal render height above 2048 overflows and the patch is forced to enable both the sprite-Y overflow repair and the background-quad depth-write hack. Together they make the background quad, now stretched over the whole backbuffer, reject every later layer with `z>0` under `D3DCMP_LESSEQUAL`: story/legend select shows a stray background layer, the camp menu is washed out by stacked layers, loading shows nothing but the base colour, and cutscene subtitles disappear. `internal_render_width` / `internal_render_height` now default to the desktop resolution (`0`/`0`) and the option reference states that **2048 is the hard limit** above which those repairs kick in.
- New `orochi_depth_fix`: how the background quad's depth write is treated — `0` leaves it alone, `1` removes it from every full-screen quad (the old behaviour), `2` removes it from the first one of each frame only (default). Only applies when the internal render height is above 2048.
- **Musou Orochi Z: pad B / OPTION now presses the keyboard Enter key.** The story select, the legend screens and the "2P JOINED" popup accept nothing but a keyboard confirm, and the pad used to do nothing there. New `pad_enter_buttons` (default `0x82` = B + OPTION), `pad_enter_mode` (default `3` = `SendInput`) and `pad_enter_pulse_ms` (default `60`). This title does not poll its DirectInput keyboard device and ignores posted window messages, so only the `WM_KEYDOWN`/`WM_CHAR`/`WM_KEYUP` that Windows itself generates from `SendInput` is recognised.

### 2.46
- **New: Dynasty Warriors 6 (`SM5_WIN.exe`)** with its own `SSM5_Tweaks.ini`. This port filters controllers by **product name**, so a device whose name is not on the game's whitelist is enumerated but never selected. The virtual pad therefore reports itself as `Controller (XBOX 360 For Windows)` (new `pad_name` option).
- The virtual DirectInput device gained `pad_axes`: `2` keeps the original left-stick-only layout, `6` also exposes Z (left trigger), Rx/Ry (right stick) and Rz (right trigger). DW6 defaults to `6` because it steers the camera with the right stick; every other title stays on `2` and behaves exactly as before.
- DW6 defaults to `dpad_pov=1` with `dpad_as_left_stick=0` and `dpad_arrow_keys=1`: the D-pad stays a plain POV hat plus four independent buttons (bindable on its own, never walks the character), and menu navigation goes through the game's own buffered DirectInput keyboard device. The main window is matched by its registered class `SangokuMusou5PC`.
- New `dpad_menu_repeat_ms` / `dpad_menu_repeat_delay_ms`: for menus that step one entry per press, a held direction is released for a short moment every period after the delay, so the game keeps receiving fresh presses and the cursor keeps moving. The repeat only reaches the POV hat, the D-pad buttons and the arrow-key injection — with `dpad_as_left_stick=0` the left stick never moves. DW6 enables it by default (120/400); every other title keeps `0` (off) and behaves as before.
- New **class-name polling fallback** for the borderless window: some builds (`SM5_WIN.exe` statically imports only `MessageBoxA`) never touch the `CreateWindowExA` IAT slot, so the original window hook never installed and the borderless resize silently did nothing. When the window hooks are missing the patch now polls the process's own top-level windows for the registered class, logs every candidate class, and takes the single large window if the expected class never appears. Live DW6 log: `main window watcher started` → `main window hooked class=SangokuMusou5PC bounds=0,0 2560x1440`.
- New `install-ssm5.ps1`; `install-targets.ps1` and `restore.ps1` learned about DW6.

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

Current build: **2.53**; public release: **V1.0.5**. Package names, sizes and SHA-256 are listed in `RELEASES.md`.
