# 老无双系列 PC 修复补丁

## 做什么

32 位 DLL 补丁，为真三国无双3（Dynasty Warriors 4: Hyper / DW4 Hyper）、真三国无双4 Special（Dynasty Warriors 5 Special / DW5 Special）、真三国无双6 猛将传 DX 和《战国无双2》提供 XInput/原生 DS4 手柄适配、按键修复、热插拔和窗口输入兼容；为真三国无双7 with 猛将传提供无边框窗口和清晰缩放。真三3 / DW4 Hyper 与真三4 / DW5 Special 保持游戏自身分辨率，仅由无边框窗口放大，拒绝高分辨率覆写。其他游戏按各自引擎配置单次主交换链输出和分辨率扩展；`single_present` 避免覆盖层重复统计。SW2 另限制为 75 FPS 并跳过关闭确认框。

## 安装

把发布包对应的 DLL 和配置放入游戏根目录。真三6 DX 可运行 `install-ssm6dx.ps1` 自动安装输入补丁和繁体版额外 MOD 并备份；恢复时使用备份目录运行 `restore.ps1`。

## 配置

配置保留英文键名并附有中文注释，修改后重启游戏。蛇魔 Z 使用 `OrochiZPadX.ini`；战国2、真三3、真三4、真三6 DX、真三7 分别使用 `SM2_Tweaks.ini`、`SSM3_Tweaks.ini`、`SSM4_Tweaks.ini`、`SSM6_Tweaks.ini`、`SSM7_Tweaks.ini`。

- `[padfix]`：`enable` 控制手柄适配，`backend` 选择自动/XInput/原生 DS4；十字键四方向独立输出为 Button 13–16，`dpad_pov` 提供 POV，`dpad_menu_navigation` 仅对已识别版本补充游戏菜单映射；`dpad_as_left_stick=0` 保持十字键与左摇杆分离。其余项目调整摇杆死区、扳机阈值和连接保护。菜单移动与游戏内单独绑定仍取决于各游戏映射表，不能把按钮输出等同于已完成菜单导航。
- `[platform]`：`window_mode` 开启无边框窗口；真三3 / DW4 Hyper 与真三4 / DW5 Special 保持游戏自身分辨率并由无边框窗口放大（拒绝高分辨率覆写）；其他游戏沿用已核验的引擎专用缩放路径。`single_present` 避免额外 Present 造成覆盖层帧数重复统计。`shared_input`、`disable_game_ime`、`com_apartment_compat`、`close_without_confirmation` 分别控制后台输入、输入法隔离、COM 兼容和关闭确认框。
