# MUSOUTweaksPC 2.47 配置说明

本文说明游戏配置。`dinput8.dll` 在游戏启动时按可执行文件选择同目录配置；修改后请完全退出并重新启动游戏。

| 游戏 | 适配版本 | 配置文件 |
|---|---|---|
| 无双蛇魔 Z (`OROCHI_Z_TC.exe`) | `V1.0.0.0` | `OrochiZPadX.ini` |
| 真三国无双3 / Dynasty Warriors 4: Hyper (`ShinSangokumusou3.exe`) | 游侠繁体汉化 `V0.1.1.0`（`2.0.0.0`） | `SSM3_Tweaks.ini` |
| 真三国无双4 Special / Dynasty Warriors 5 Special (`SM4_WIN.exe`、`SS4*.exe`) | `V1.0.0.0` | `SSM4_Tweaks.ini` |
| 真三国无双5 / Dynasty Warriors 6 (`SM5_WIN.exe`) | `V1.0.0.1` | `SSM5_Tweaks.ini` |
| 真三国无双6 猛将传 DX (`SM6TC.exe`、`SM6EN.exe`、`SM6JP.exe`) | Steam `V1.0.0.0` | `SSM6_Tweaks.ini` |
| 战国无双2 (`SW2.exe`) | `V1.0.0.0` | `SM2_Tweaks.ini` |
| 真三国无双7 / Dynasty Warriors 8 (`Launch.exe`) | Steam `V1.0.4.0` | `SSM7_Tweaks.ini` |

「适配版本」是实际验证过的游戏版本；补丁按可执行文件名和内存特征码识别，其他版本大概率也能用，失效时把 `log` 改成 `1` 看日志里哪一步没匹配上。

## 主产物与文件

- `dinput8.dll`：主产物。放在对应游戏可执行文件同一目录，由游戏自动加载。
- 上表所列 `.ini`：可选配置文件，和 DLL 放在同一目录。
- `MUSOUTweaksPC.log`：诊断日志。默认不生成，仅在 `log=1` 时写入。

配置项使用 `名称=数值` 格式。`1` 表示开启，`0` 表示关闭。缺少配置项时会采用本文列出的默认值；超出允许范围的数值会自动限制到有效范围。

## 手柄与按键

这些项目位于 `[padfix]`。

| 配置项 | 默认值 | 作用与可用值 |
|---|---:|---|
| `enable` | `1` | 开启虚拟 DirectInput 手柄适配。设为 `0` 后不再替换游戏的 DirectInput 接口；`[platform]` 下的窗口、输入法等功能仍独立生效。|
| `log` | `0` | 是否写入 `MUSOUTweaksPC.log`。默认 `0` 不写日志；需要排障时改为 `1` 后重启游戏。|
| `backend` | `0` | 手柄来源：`0` 自动（优先 XInput，未找到时选择原生 Sony DS4）；`1` 只使用 XInput；`2` 只使用原生 Sony DS4。自动模式连接后保持当前来源，断开后再重新选择。|
| `xinput_slot` | `-1` | XInput 手柄槽位。`-1` 自动选第一个在线槽位；`0`–`3` 固定使用对应槽位。指定槽位会优先于自动选择。|
| `foreground_only` | `1` | 仅游戏位于前台时把手柄状态交给游戏。建议保持开启，以免切到桌面或其他程序时游戏仍接收手柄输入。|
| `pad_name` | 按游戏 | 虚拟手柄向游戏报告的产品名。空值等于自动：真三5 取 `Controller (XBOX 360 For Windows)`，其余作品取 `MUSOUTweaksPC Gamepad`。真三5 只认它内置名单里的手柄名（Xbox 360 手柄、罗技 ChillStream、ELECOM/SANWA 等），名字不在名单里时游戏能枚举到设备但不会选中，表现成手柄完全没反应。|
| `pad_axes` | `0` | 虚拟手柄暴露的轴数量：`2` 只有左摇杆（老行为），`6` 再加上扳机与右摇杆（X/Y/Z/Rx/Ry/Rz）。`0` 或 `-1` 自动：真三5 取 `6`（该作用右摇杆转视角），其余作品取 `2`。|
| `dpad_menu_navigation` | `1` | 对已识别的游戏版本，将十字键加入独立菜单方向映射；未识别的版本会跳过菜单映射。不会把十字键伪装成键盘方向键。|
| `dpad_as_left_stick` | `0` | `0` 保持十字键按钮/POV 与左摇杆轴独立；`1` 才额外同步方向到左摇杆轴；`2` 只在方向变化时点动一小段。左摇杆是移动轴，桥接后角色会跟着走，只有菜单导航无路可走时才用 `1`/`2`。**所有作品（含真三5）默认都是 `0`**：十字键只走 POV 方向帽和独立按钮，可以单独绑键，也不会带动角色。菜单光标走 `dpad_arrow_keys`。|
| `dpad_menu_pulse_ms` | `50` | `dpad_as_left_stick=2` 的点动脉冲长度，单位毫秒，范围 `0`–`1000`。同时用作长按连发的松开时长。|
| `dpad_menu_repeat_ms` | `0`（真三5 为 `120`） | 长按连发周期，单位毫秒，范围 `0`–`1000`；`0` 关闭连发。菜单是「按一次走一格」的类型时，按住不放光标就不动了，开启后补丁会把持续按住的方向周期性松开 `dpad_menu_pulse_ms` 毫秒，游戏就会收到一次新的「按下」，光标持续移动。连发作用于 POV 方向帽、四个十字键按钮和方向键注入；只有显式开了 `dpad_as_left_stick` 才会影响到摇杆轴，默认 `0` 时左摇杆完全不动。|
| `dpad_menu_repeat_delay_ms` | `400` | 开始连发前的等待时间，单位毫秒，范围 `0`–`3000`。短按不受影响，按住超过这个时间才开始重复。|
| `dpad_arrow_keys` | `-1` | 十字键改用键盘方向键导航菜单的方式。`0` 关闭；`1` 在游戏自己的 DirectInput 键盘设备上按下方向键；`2` 用 `SendInput` 投递真实的方向键按键（走窗口消息循环）；`-1` 自动（真三6 DX 与真三5 取 `1`，战国2 取 `2`）。真三6 DX 与真三5 以缓冲模式读键盘（`GetDeviceData`），补丁成对追加 `DIK_UP/DOWN/LEFT/RIGHT` 的按下与松开事件。战国2 从 `USER32` 只导入了 `MessageBoxA`，既不读键盘状态也不轮询它自己创建的键盘设备，所以只能走 `2`：`SendInput` 注入到与物理按键完全相同的一层，系统自己把 `WM_KEYDOWN` 交给前台窗口。两种方式下左摇杆都不动，十字键本身仍作为 POV/按钮可单独绑键。|
| `dpad_arrow_pulse_ms` | `40` | 仅 `dpad_arrow_keys=2` 有效：合成按键不会自动重复，所以按住方向时补丁把它切成一次次短按。这是单次按下的时长，单位毫秒，范围 `10`–`500`。|
| `dpad_arrow_gap_ms` | `80` | 仅 `dpad_arrow_keys=2` 有效：两次按下之间松开多久，单位毫秒，范围 `10`–`1000`。它与 `dpad_arrow_pulse_ms` 相加就是一个菜单步进的周期（默认 120 毫秒，约每秒 8 格）。|
| `left_deadzone` | `20` | 左摇杆死区百分比，范围 `0`–`90`。数值调低会更灵敏，也更容易受摇杆漂移影响；调高会减少漂移，但需要推得更远。|
| `right_threshold` | `55` | 右摇杆触发数字方向按钮的阈值百分比，范围 `10`–`95`。降低会更容易触发视角方向，提高可减少轻推时误触。右摇杆映射为四个独立按钮，不是连续模拟轴。|
| `trigger_threshold` | `40` | LT/RT 触发按钮的阈值，范围 `1`–`254`。数值较低时扳机轻压即可触发。|
| `neutral_ms` | `250` | 手柄连接或游戏重新获得焦点后，要求所有按键松开、摇杆居中的保护时间，单位毫秒，范围 `0`–`5000`。设为 `0` 可关闭等待；建议保留默认值。|
| `ds4_right_x` | `2` | 原生 DS4 右摇杆 X 轴索引，范围 `0`–`5`。索引依次为 X、Y、Z、Rx、Ry、Rz。|
| `ds4_right_y` | `5` | 原生 DS4 右摇杆 Y 轴索引，范围 `0`–`5`。标准 DS4 默认使用 Z/Rz（`2`/`5`）。仅当特定原生 DS4 驱动的轴顺序不同时修改。|

十字键始终作为独立 DirectInput 按钮提供：上/下/左/右对应 Button 13/14/15/16，并可另行暴露为 POV。`dpad_as_left_stick=0` 时它们不会进入左摇杆轴，因此不会驱动战斗移动。真三6 DX 的菜单不认按钮与 POV，改为用 `dpad_arrow_keys` 在游戏的键盘设备上按下方向键来移动光标；战国2 的菜单同样不认按钮与 POV，但它的键盘只能从窗口消息拿到，所以用 `dpad_arrow_keys=2`（`SendInput`）送方向键。

当前适配只向游戏呈现一个虚拟手柄；双人双手柄、DualSense、DS4 仿制品以及其他 DirectInput 设备未适配。使用 DS4Windows 等工具将 DS4 转成 Xbox/XInput 时，应选择 `backend=1` 或保持自动模式，而不是 `backend=2`。

## 窗口、后台输入和启动兼容

这些项目位于 `[platform]`。

| 配置项 | 默认值 | 作用与可用值 |
|---|---:|---|
| `shared_input` | `1` | 将游戏键盘/鼠标输入从独占改为非独占，并取消鼠标限制。开启后，游戏处于前台时，后台注册的全局快捷键应用也可以响应；不会把游戏按键广播到任意后台文本框。设为 `0` 可恢复游戏原有的独占输入请求。|
| `window_mode` | `1` | `1` 使用主显示器原生分辨率的无边框全屏窗口；`0` 保留游戏原本的显示模式和窗口行为。|
| `render_width` | `0` | 无边框模式下的渲染宽度（像素）。`0` 自动采用目标显示器桌面宽度；自定义值为 `640`–`8192`。真三3 / DW4 Hyper 与真三4 / DW5 Special 会拒绝自定义渲染尺寸，保持游戏自身分辨率。|
| `render_height` | `0` | 无边框模式下的渲染高度（像素）。`0` 自动采用目标显示器桌面高度；自定义值为 `360`–`8192`。真三3 / DW4 Hyper 与真三4 / DW5 Special 会拒绝自定义渲染尺寸，保持游戏自身分辨率。|
| `native_resolution` | `0` | `1` 使用游戏所在显示器桌面尺寸作为无边框后备缓冲区；`0` 保持游戏所选缓冲区尺寸。真三3 / DW4 Hyper 与真三4 / DW5 Special 会强制保持游戏自身缓冲区（该项被忽略），避免高分辨率路径导致菜单、片头与战场坐标错位或黑屏。**真三6 DX 也必须保持 `0`**：该作只往放大后后备缓冲区的左上角 1920×1080 区域绘制，右边和下边留黑。|
| `single_present` | `1` | 每帧只通过游戏主交换链提交一次，避免额外输出链让 Steam/RTSS 重复统计帧数，并保留游戏亮度与覆盖层路径。设为 `0` 可恢复单独的桌面输出交换链。显式高于桌面的缓冲区仍保留高分辨率渲染。|
| `scale_to_fill` | `0` | `1` 对已完成画面执行补丁缩放；`0` 关闭后处理缩放。真三3 / DW4 Hyper 与真三4 / DW5 Special 不参与补丁缩放，画面由无边框窗口自身拉伸到桌面。|
| `scale_aspect_mode` | `0` | 仅在补丁自定义缩放时生效。`0` 拉伸完整画面；`1` 等比居中并以柔化背景填充。真三3、真三4 不使用该项。|
| `scale_filter` | `2` | 缩放滤镜：`0` 点采样；`1` 线性滤镜；`2` Catmull-Rom 立方重建，兼顾边缘清晰度与平滑度。用于 D3D9 主交换链缩放。|
| `orochi_depth_fix` | `2` | 仅蛇魔Z，且仅在内部渲染高度 > 2048 时生效。`0` 完全不改全屏四边形的深度写入；`1` 每帧所有全屏四边形都禁止写入（2.47 之前的行为）；`2` 只禁止每帧第一个全屏四边形写入。背景四边形按 `D3DCMP_LESSEQUAL` 与 `z=0` 判定，禁止写入后 `z>0` 的 3D 场景、loading 文字、字幕才能通过深度测试；但过渡/压暗层同样是全屏四边形，一起禁止就会整屏多层叠加发白。|
| `pad_enter_buttons` | `-1` | 哪些手柄按键同时按下键盘 Enter，按 `buttonMasks` 位序取掩码（bit0=A、bit1=B、bit7=OPTION/Start）。`-1` 自动：蛇魔Z 为 `0x82`（B + OPTION），其余作品为 `0`（关闭）。|
| `pad_enter_mode` | `-1` | Enter 的注入路径。`1` 在游戏自己的 DirectInput 键盘设备上按下（游戏必须轮询该设备）；`2` 用 `PostMessageA` 投递 `WM_KEYDOWN`/`WM_CHAR`/`WM_KEYUP`；`3` 用 `SendInput`，由系统输入栈生成真实键消息投给前台窗口（蛇魔Z 默认）。`-1` 自动：蛇魔Z 为 `3`，其余为 `1`。|
| `pad_enter_pulse_ms` | `60` | Enter 的按下时长，10~500 毫秒。菜单确认需要「按一下」而不是「一直按住」，到时间自动松开。|
| `frame_capture` | `0` | 排障用。置 `1` 后在游戏目录创建 `MUSOUTweaksPC_capture.flag`，下一帧会从后备缓冲直接写出 `MUSOUTweaksPC_frame.bmp`，再自动删除标记文件。桌面截屏拿不到游戏画面时用它取真实帧。|
| `internal_render_width` / `internal_render_height` | `0` | 蛇魔Z、真三7、SW2 各自的专用补丁读取此项；`0` 表示跟随桌面分辨率。真三3 / DW4 Hyper 与真三4 / DW5 Special 会拒绝非零值并回落到游戏自身分辨率。**高度不要超过 2048**：引擎精灵坐标是 12.4 定点数，超过即溢出，补丁会被迫启用精灵 Y 修补和背景深度写入修补，两者叠加会破坏图层顺序。|
| `sw2_fix_hud` | `1` | SW2 专用分辨率补丁的 HUD/菜单坐标校正。`1` 开启，`0` 关闭。|
| `sw2_camera_mode` | `0` | SW2 镜头比例实验选项：`0` 保持游戏原镜头代码；`1`/`2` 选择两种备用比例路径。上游说明目标可能命中副镜头，未单独验证前建议保持 `0`。|
| `disable_game_ime` | `1` | 隔离游戏进程内的输入法上下文，绕过中文 IME 参与游戏初始化时出现的启动崩溃。不会切换系统输入法，也不支持在游戏内部输入中文。设为 `0` 可恢复游戏原有 IME 行为。|
| `com_apartment_compat` | `1` | 兼容游戏初始化时遇到的 COM apartment 模式冲突。修复只针对已确认的游戏调用点和 `RPC_E_CHANGED_MODE`；其他 COM 错误不会被忽略。建议保持开启。|
| `close_without_confirmation` | `1` | 关闭游戏窗口时绕过游戏的关闭确认框，并继续走游戏正常销毁/退出流程。设为 `0` 可恢复原确认框。|
| `fps_limit` | `0`（战国2 为 `65`） | D3D9 Present 帧率上限，范围 `0`–`240`；`0` 不限帧。`SW2.exe` 的程序内默认值就是 `65`，即使 ini 里没有这一项也按 65 FPS 节拍提交画面。它不调用系统显示模式切换 API，不修改 Windows 桌面分辨率或刷新率。|

自定义分辨率示例：

```ini
[platform]
window_mode=1
render_width=1920
render_height=1080
```

`render_width` 和 `render_height` 可分别设为 `0`。蛇魔 Z 与 SW2 使用各自的游戏专用分辨率钩子。真三3 / DW4 Hyper 与真三4 / DW5 Special 不做任何分辨率覆写：两作以游戏自身 1280×960 后备缓冲区渲染，只由无边框窗口把画面放大到桌面，`window_mode`、手柄、输入法、共享输入与关闭确认等功能保持有效。这两作的引擎把界面与场景坐标绑在所选显示模式上，扩大后备缓冲区会裁切画面、错位菜单，真三4 还会黑屏，因此补丁在日志中记录 `legacy resolution override refused` 并忽略 `render_width` / `render_height` / `native_resolution` / `scale_to_fill` / `internal_render_*`。

## 蛇魔 Z 独立内部分辨率

`OrochiZPadX.ini` 的 `internal_render_width` / `internal_render_height` 让已核验的蛇魔 Z 引擎宽高查询和运行时宽高变量使用指定尺寸，由游戏主交换链呈现到桌面。这是对真三6 DX 分辨率 MOD 配置驱动宽高行为的游戏专用移植；蛇魔 Z 使用自己的查询函数和运行时尺寸变量，不套用 SM6TC 的地址。两项同时设为 `0` 时沿用 `render_width` / `render_height`（默认即桌面分辨率）。显示器分辨率和刷新率不变；更高渲染尺寸会增加 GPU 与显存负担。

**高度上限 2048。** 引擎的 GS 精灵坐标是 12.4 定点数（12 位整数 + 4 位小数），高度超过 2048 后坐标回绕，补丁会被迫启用两项修补：把回绕的 Y 坐标搬回屏幕内，以及临时关闭全屏背景四边形的深度写入。这两项修补在 3840×2160 下无法同时保持图层顺序——背景四边形铺满后备缓冲后按 `D3DCMP_LESSEQUAL` 与 `z=0` 判定，后面所有 `z>0` 的 UI 与 3D 都会被判死，只能靠关掉深度写入放行；而关掉之后过渡层、压暗层又全部透出来。实际症状是选择故事/列传多出一层背景、兵营整屏发白、loading 只剩余底色、即时演算字幕消失。因此**推荐直接使用桌面分辨率**，它是引擎能干净处理的最大尺寸。

## 真三6 DX 额外补丁

仅 `SM6TC.exe` 会由 `install-ssm6dx.ps1` 安装 `Fix_for_SM6.dll`（修复面部表情）。它由 `d3dx9_43.dll` Mod Loader 从 `mods` 子目录加载，`enable.txt` 设为 `1` 启用、`0` 禁用（注意加载器会自己重建缺失的 `enable.txt`，要彻底禁用得把 `.dll` 和目录一起移出 `mods`）。扩展 DLL 为随项目提供的二进制，未提供源码。

随包提供的 `resolution_for_SM6TC.dll` **已不再安装**：它把引擎内部渲染分辨率改成桌面尺寸而游戏的逻辑分辨率不变，会造成 3D 与 2D 层错位（视野异常），详见 `THIRD_PARTY_MODS.md`。

## 常用配置组合

### 默认推荐（Xbox XInput 或原生 DS4）

```ini
[padfix]
enable=1
backend=0
xinput_slot=-1
foreground_only=1
dpad_menu_navigation=1

[platform]
shared_input=1
window_mode=1
render_width=0
render_height=0
disable_game_ime=1
com_apartment_compat=1
close_without_confirmation=1
```

### 只使用 Xbox/XInput

```ini
[padfix]
backend=1
xinput_slot=-1
```

如需固定某个已连接 Xbox 手柄，可将 `xinput_slot=-1` 改成 `0`、`1`、`2` 或 `3`。

### 只使用通过 USB/蓝牙直连的原生 Sony DS4

```ini
[padfix]
backend=2
ds4_right_x=2
ds4_right_y=5
```

该模式要求系统能以原生 DS4 DirectInput 设备枚举到 VID `054C`、PID `05C4` 或 `09CC`。

### 恢复游戏原有窗口与关闭确认

```ini
[platform]
window_mode=0
close_without_confirmation=0
```

## 修改步骤与恢复

1. 正常退出游戏。
2. 用文本编辑器修改上表对应的游戏配置文件，保存为普通文本。保留 `[padfix]`、`[platform]` 区段名。
3. 启动游戏。配置只在启动时读取，游戏运行期间改文件不会即时生效。
4. 若需恢复原版本，先退出游戏，再运行项目包中的 `restore.ps1` 并指定安装时生成的 `MUSOUTweaksPC-backup-时间戳` 备份目录。

配置文件无法修复不受支持的游戏 EXE 指纹；修改 DLL、EXE 或菜单映射代码并非普通配置操作。出现问题时先恢复默认 INI，并查看同目录 `MUSOUTweaksPC.log`。

### 真三7镜头远近

`SSM7_Tweaks.ini` 的 `[platform]` 新增 `camera_distance_percent=100`。随附配置设为 `125`。范围 `50` 到 `300`；`100` 保持原始镜头，`125` 稍微拉远，`150` 进一步拉远。此项沿原视线移动 3D 视点，保持 FOV、人物比例和 HUD 尺寸。游戏运行中保存配置，约半秒后读取；移动角色或转动视角时更新缓存的镜头。过大的倍率可能退入墙后，原生镜头避障距离不会同步扩展。

Camera distance uses `camera_distance_percent` (50–300, default 100). It moves the 3D viewpoint without changing the lens or HUD. Settings reload within 500 ms; move the character/camera to refresh cached views. Large values may move behind walls.

镜头快捷键默认开启（`camera_hotkeys=1`），仅在游戏前台响应：`Ctrl+Alt+PageUp` 拉远、`Ctrl+Alt+PageDown` 拉近、`Ctrl+Alt+Home` 恢复 `100`。`camera_distance_step=5` 表示每次变化 5 个百分点；按住可连续调整，结果自动保存到 `camera_distance_percent`。

`camera_hotkey_modifiers` 为修饰键位掩码：Ctrl=`1`、Alt=`2`、Shift=`4`，相加组合，默认 `3`；`0` 不要求修饰键。`camera_hotkey_farther` / `camera_hotkey_nearer` / `camera_hotkey_reset` 使用十进制 Windows 虚拟键码，默认 `33`（PageUp）、`34`（PageDown）、`36`（Home）；对应设为 `0` 禁用该动作。上述选项也支持运行中修改。

Camera shortcuts work only in the game foreground. Ctrl+Alt+PageUp/PageDown moves farther/nearer; Ctrl+Alt+Home resets to 100. Hold to repeat; each step defaults to 5 percentage points. Changes persist to the INI. Modifier bits are Ctrl=1, Alt=2, Shift=4; key options use Windows virtual-key codes, with 0 disabling the action.

### 蛇魔Z战斗镜头

`OrochiZPadX.ini` 的 `[platform]` 支持与真三7相同的 `camera_distance_percent`、`camera_hotkeys`、`camera_distance_step` 和快捷键配置，默认镜头为 `125`。仅调整玩家战斗镜头，CPU 投影与 PC 绘制镜头同步修改，准备／军团情报地图不参与缩放。快捷键为 Ctrl+Alt+PageUp 拉远、Ctrl+Alt+PageDown 拉近、Ctrl+Alt+Home 恢复原始距离，调整结果自动保存。

Orochi Z shares the camera configuration and foreground shortcuts with SSM7. Only player battle views are adjusted; CPU projection and renderer cameras use the same temporary viewpoint, while preparation maps retain their own framing.
