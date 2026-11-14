# OrochiZPadX 2.9 自定义配置说明

本文说明游戏配置。`dinput8.dll` 在游戏启动时按可执行文件选择同目录配置；修改后请完全退出并重新启动游戏。

| 游戏 | 配置文件 |
|---|---|
| 无双蛇魔 Z (`OROCHI_Z_TC.exe`) | `OrochiZPadX.ini` |
| 真三国无双3 (`ShinSangokumusou3.exe`) | `SSM3_Tweaks.ini` |
| 真三国无双4 (`SM4_WIN.exe`、`SS4*.exe`) | `SSM4_Tweaks.ini` |
| 真三国无双7 / Dynasty Warriors 8 (`Launch.exe`) | `SSM7_Tweaks.ini` |

## 主产物与文件

- `dinput8.dll`：主产物。放在对应游戏可执行文件同一目录，由游戏自动加载。
- 上表所列 `.ini`：可选配置文件，和 DLL 放在同一目录。
- `OrochiZPadX.log`：诊断日志。仅在 `log=1` 时写入。

配置项使用 `名称=数值` 格式。`1` 表示开启，`0` 表示关闭。缺少配置项时会采用本文列出的默认值；超出允许范围的数值会自动限制到有效范围。

## 手柄与按键

这些项目位于 `[padfix]`。

| 配置项 | 默认值 | 作用与可用值 |
|---|---:|---|
| `enable` | `1` | 开启虚拟 DirectInput 手柄适配。设为 `0` 后不再替换游戏的 DirectInput 接口；`[platform]` 下的窗口、输入法等功能仍独立生效。|
| `log` | `1` | 写入 `OrochiZPadX.log`。排障时保持 `1`；不需要日志可设为 `0`。|
| `backend` | `0` | 手柄来源：`0` 自动（优先 XInput，未找到时选择原生 Sony DS4）；`1` 只使用 XInput；`2` 只使用原生 Sony DS4。自动模式连接后保持当前来源，断开后再重新选择。|
| `xinput_slot` | `-1` | XInput 手柄槽位。`-1` 自动选第一个在线槽位；`0`–`3` 固定使用对应槽位。指定槽位会优先于自动选择。|
| `foreground_only` | `1` | 仅游戏位于前台时把手柄状态交给游戏。建议保持开启，以免切到桌面或其他程序时游戏仍接收手柄输入。|
| `dpad_menu_navigation` | `1` | 将十字键加入游戏独立的菜单方向映射。`0` 关闭这项菜单补充；不会把十字键伪装成键盘方向键。|
| `dpad_as_left_stick` | `-1` | `-1` 自动（真三国无双3开启，其他游戏关闭）；`1` 将十字键同步为左摇杆方向，`0` 关闭。适用于没有读取 POV 十字键映射的真三3 PC 版。|
| `left_deadzone` | `20` | 左摇杆死区百分比，范围 `0`–`90`。数值调低会更灵敏，也更容易受摇杆漂移影响；调高会减少漂移，但需要推得更远。|
| `right_threshold` | `55` | 右摇杆触发数字方向按钮的阈值百分比，范围 `10`–`95`。降低会更容易触发视角方向，提高可减少轻推时误触。右摇杆映射为四个独立按钮，不是连续模拟轴。|
| `trigger_threshold` | `40` | LT/RT 触发按钮的阈值，范围 `1`–`254`。数值较低时扳机轻压即可触发。|
| `neutral_ms` | `250` | 手柄连接或游戏重新获得焦点后，要求所有按键松开、摇杆居中的保护时间，单位毫秒，范围 `0`–`5000`。设为 `0` 可关闭等待；建议保留默认值。|
| `ds4_right_x` | `2` | 原生 DS4 右摇杆 X 轴索引，范围 `0`–`5`。索引依次为 X、Y、Z、Rx、Ry、Rz。|
| `ds4_right_y` | `5` | 原生 DS4 右摇杆 Y 轴索引，范围 `0`–`5`。标准 DS4 默认使用 Z/Rz（`2`/`5`）。仅当特定原生 DS4 驱动的轴顺序不同时修改。|

当前适配只向游戏呈现一个虚拟手柄；双人双手柄、DualSense、DS4 仿制品以及其他 DirectInput 设备未适配。使用 DS4Windows 等工具将 DS4 转成 Xbox/XInput 时，应选择 `backend=1` 或保持自动模式，而不是 `backend=2`。

## 窗口、后台输入和启动兼容

这些项目位于 `[platform]`。

| 配置项 | 默认值 | 作用与可用值 |
|---|---:|---|
| `shared_input` | `1` | 将游戏键盘/鼠标输入从独占改为非独占，并取消鼠标限制。开启后，游戏处于前台时，后台注册的全局快捷键应用也可以响应；不会把游戏按键广播到任意后台文本框。设为 `0` 可恢复游戏原有的独占输入请求。|
| `window_mode` | `1` | `1` 使用主显示器原生分辨率的无边框全屏窗口；`0` 保留游戏原本的显示模式和窗口行为。|
| `render_width` | `0` | 无边框模式下的渲染宽度（像素）。蛇魔 Z 中 `0` 自动采用主显示器宽度；其他游戏中 `0` 保留游戏画面尺寸。自定义值为 `640`–`8192`。|
| `render_height` | `0` | 无边框模式下的渲染高度（像素）。蛇魔 Z 中 `0` 自动采用主显示器高度；其他游戏中 `0` 保留游戏画面尺寸。自定义值为 `360`–`8192`。|
| `native_resolution` | `0` | `1` 让 D3D9 后备缓冲区跟随游戏所在显示器分辨率；`0` 保持游戏所选缓冲区尺寸。若启用 `scale_to_fill`，游戏原始视口保持不变。|
| `scale_to_fill` | `0` | `1` 把游戏逻辑渲染区域放大铺满原生后备缓冲区；`0` 不进行铺满缩放。仅 DW8 配置默认开启。|
| `scale_filter` | `2` | 缩放滤镜：`0` 点采样；`1` 线性滤镜；`2` Catmull-Rom 立方重建，兼顾边缘清晰度与平滑度。仅在 `scale_to_fill=1` 时使用。|
| `disable_game_ime` | `1` | 隔离游戏进程内的输入法上下文，绕过中文 IME 参与游戏初始化时出现的启动崩溃。不会切换系统输入法，也不支持在游戏内部输入中文。设为 `0` 可恢复游戏原有 IME 行为。|
| `com_apartment_compat` | `1` | 兼容游戏初始化时遇到的 COM apartment 模式冲突。修复只针对已确认的游戏调用点和 `RPC_E_CHANGED_MODE`；其他 COM 错误不会被忽略。建议保持开启。|
| `close_without_confirmation` | `1` | 关闭游戏窗口时绕过游戏的关闭确认框，并继续走游戏正常销毁/退出流程。设为 `0` 可恢复原确认框。|

自定义分辨率示例：

```ini
[platform]
window_mode=1
render_width=1920
render_height=1080
```

`render_width` 和 `render_height` 可分别设为 `0`。蛇魔 Z 此时采用主显示器尺寸并由已验证的游戏专用分辨率钩子更新内部尺寸；其他旧游戏保留引擎当前画面尺寸。DW8 的配置器最高列出 1920×1200，而显示器是 2560×1440，因此其配置使用 `native_resolution=1` 创建原生缓冲区，再由 `scale_to_fill=1` 将逻辑画面铺满；默认点采样避免线性插值额外柔化画面，但 1080p 到 1440p 放大仍会有轻微像素感，不会凭空增加纹理细节。显卡或显存无法创建目标尺寸时，游戏仍会返回真实的 Direct3D 错误。不同宽高比与 HUD 排列仍需逐项验收。

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
4. 若需恢复原版本，先退出游戏，再运行项目包中的 `restore.ps1` 并指定安装时生成的 `OrochiZPadX-backup-时间戳` 备份目录。

配置文件无法修复不受支持的游戏 EXE 指纹；修改 DLL、EXE 或菜单映射代码并非普通配置操作。出现问题时先恢复默认 INI，并查看同目录 `OrochiZPadX.log`。
