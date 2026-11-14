# 无双蛇魔 Z 修复补丁

## 做什么

32 位 `dinput8.dll` 代理补丁，修复游戏的 Xbox XInput／原生 DS4 手柄输入与映射；支持十字键菜单导航、右摇杆四方向、手柄热插拔、后台全局快捷键、无边框高分辨率窗口，并规避中文输入法启动崩溃和关闭确认框。适用已验证的 `OROCHI_Z_TC.exe`。

## 安装

1. 退出游戏，并备份游戏目录中已有的 `dinput8.dll`。
2. 将本项目的 `dinput8.dll` 和 `OrochiZPadX.ini` 复制到 `OROCHI_Z_TC.exe` 所在目录。
3. 启动游戏。修改配置前先退出游戏，改完后重新启动。

卸载时退出游戏并删除补丁 DLL 与 INI，再将备份 DLL 放回原位。

## 配置项

配置文件：`OrochiZPadX.ini`。所有注释包含英文和中文；`1` 开启、`0` 关闭。

### `[padfix]` 手柄

- `enable`：启用手柄适配；关闭后 `[platform]` 功能仍可用。
- `log`：写入同目录 `OrochiZPadX.log`。
- `backend`：`0` 自动（优先 XInput，再原生 DS4）；`1` 仅 XInput；`2` 仅原生 DS4。
- `xinput_slot`：`-1` 自动选择；`0`–`3` 固定 XInput 槽位。
- `foreground_only`：只在游戏前台传递手柄输入。
- `dpad_menu_navigation`：启用独立的菜单十字键映射，不模拟键盘方向键。
- `left_deadzone`：左摇杆死区百分比，范围 `0`–`90`。
- `right_threshold`：右摇杆数字方向触发阈值百分比，范围 `10`–`95`。
- `trigger_threshold`：LT/RT 触发阈值，范围 `1`–`254`。
- `neutral_ms`：连接或切回后松键、摇杆居中等待时间，范围 `0`–`5000` 毫秒。
- `ds4_right_x`、`ds4_right_y`：原生 DS4 右摇杆轴索引，范围 `0`–`5`；默认 `2`/`5`（Z/Rz）。

### `[platform]` 窗口与兼容

- `shared_input`：非独占键鼠输入，允许后台全局快捷键应用响应。
- `window_mode`：`1` 无边框全屏窗口；`0` 游戏原始显示模式。
- `render_width`、`render_height`：渲染尺寸像素；`0` 使用主显示器尺寸。自定义值分别限制在 `640`–`8192`、`360`–`8192`。
- `disable_game_ime`：隔离游戏内输入法，避免中文输入法导致启动崩溃；游戏内不能输入中文。
- `com_apartment_compat`：兼容已确认的 COM 线程模式冲突，建议开启。
- `close_without_confirmation`：关闭窗口时跳过游戏确认框，保留正常退出流程。
