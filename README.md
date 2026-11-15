# 无双系列 PC 修复补丁

## 做什么

32 位 DLL 补丁，为真三国无双3、真三国无双4 和《战国无双2》提供 XInput/原生 DS4 手柄适配、按键修复、热插拔与窗口输入兼容；为《Dynasty Warriors 8》（真三国无双7）启用无边框窗口和清晰缩放。SW2 配置额外将游戏帧节拍限制到 75 FPS，且不切换 Windows 桌面分辨率或刷新率，并跳过关闭游戏时的确认框。

## 安装

退出游戏后，在项目目录运行 `install-targets.ps1` 安装所有支持目标；只安装战国无双2时运行 `install-sw2.ps1`。脚本会为被替换的文件生成带 manifest 的时间戳备份。

使用 Visual Studio x86 C++ 环境运行 `build.cmd` 与 `build-platform.cmd`。主产物为 `build\dinput8.dll`，DW8 使用 `build\dinput8-platform.dll`。卸载时按备份 manifest 运行 `restore.ps1`。完整配置说明见 `CONFIGURATION.md`。

## 配置

各游戏目录的配置保留英文键名并附有中文注释，改动后重启游戏。蛇魔 Z 使用 `OrochiZPadX.ini`，战国无双2 使用 `SM2_Tweaks.ini`，真三3 使用 `SSM3_Tweaks.ini`，真三4 使用 `SSM4_Tweaks.ini`，真三7 使用 `SSM7_Tweaks.ini`。

- `[padfix]`：`enable` 控制手柄适配；`backend` 选择自动/XInput/原生 DS4；`dpad_menu_navigation` 补充菜单十字键；`dpad_as_left_stick=-1` 自动只在真三3把十字键同步到左摇杆；`dpad_pov`、`left_deadzone`、`right_threshold`、`trigger_threshold`、`neutral_ms` 和 `ds4_right_x/y` 控制方向帽、摇杆、扳机、连接保护和 DS4 轴位。
- `[platform]`：`window_mode` 开启无边框窗口；`render_width/height=0` 通常保留旧游戏自己的逻辑尺寸；`native_resolution=1` 让 DW8 使用显示器分辨率输出链，`scale_to_fill=1` 将原生逻辑画面铺满，`scale_filter=2` 使用清晰的 Catmull-Rom 缩放；`shared_input=1` 解除键鼠独占和光标限制，便于前台游戏运行时使用后台热键；`disable_game_ime`、`com_apartment_compat`、`close_without_confirmation` 分别控制游戏内输入法隔离、COM 兼容和关闭确认框。
