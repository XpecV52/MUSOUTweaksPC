# 本地与远程功能差异 / Local and Remote Feature Differences

本文记录当前本地发布分支与本机登记远程的功能差异。比较基准已在发布前抓取。
This file records functional differences between the local release branch and the remotes configured on this machine. The references were fetched before the release build.

## 比较基准 / References

| 来源 | 提交 | 说明 |
| --- | --- | --- |
| 本地发布分支 / local release branch | `codex/input-window-rumble-fixes` | V1.0.5 发布候选与当前工作树 |
| GitHub `origin/main` | `1022d7d1eb6194feb628da7a1b714adaaad6bca1` | MUSOUTweaksPC 主仓库基线 |
| GitHub `orochi/master` | `d21d212c42c30e9484d65bb52f67b951d967a2c8` | 旧版蛇魔Z单游戏基线 |

## 相对 `origin/main` 的本地新增功能 / Added locally

- 新增统一输入层，可把 XInput 和原生 Sony DS4 映射为游戏可识别的 DirectInput 虚拟手柄。
  Adds one shared input layer that maps XInput and native Sony DS4 to a game-compatible DirectInput pad.
- 新增 `controller_takeover` 与 `player2_xinput_slot`。开启时1P手柄接管公共菜单，2P键盘或第二手柄写入独立动作位；关闭时跳过全部新增接管路由并恢复原生输入。
  Adds global ownership and optional second-slot configuration. The disabled state skips the added routing and restores native ownership.
- 新增真三3、真三4、真三5、真三6DX、战国2和蛇魔Z的选人、准备、商店、设置及子菜单输入路由。
  Adds per-title selector, preparation, shop, settings, and child-menu routing for the co-op ports.
- 新增 Xbox、DS4、DualSense 的 DirectInput 力反馈到双马达桥接，真三7使用平台专用构建并保留原生控制器输入。
  Adds dual-motor force-feedback bridging for Xbox, DS4, and DualSense while DW8 retains its native controller path.
- 新增无边框窗口任务栏激活后的焦点恢复、Steam/RTSS 单次 Present、输入法和 COM 兼容处理。
  Adds taskbar focus recovery for borderless windows, single-present overlay handling, IME isolation, and COM compatibility.
- 新增真三5扳机轴/召马、十字键和右摇杆路径，真三6DX镜头快捷键，战国2菜单方向注入，以及真三3小地图放大与热键。
  Adds SM5 trigger/horse-call, D-pad, and right-stick handling, SSM6 DX camera shortcuts, SW2 menu arrows, and SSM3 minimap scaling.
- 新增回归测试、安装备份与校验脚本，以及按游戏拆分的 V1.0.5 发布包。
  Adds regression tests, backup/hash-verified installers, and the per-game V1.0.5 release bundle.

## 相对 `origin/main` 的修改功能 / Modified locally

- 原来的单一虚拟手柄轮询改为按游戏和玩家分离输入来源。双人战斗期间临时交换或过滤映射，采样结束恢复原表。
  The former single-pad polling path now separates sources by title and player. Temporary battle-map changes are restored after sampling.
- 菜单方向从单一路径改为按游戏选择原生动作表、DirectInput缓冲键盘或 `SendInput`，避免十字键同时驱动角色和菜单。
  Menu directions now use a title-specific native map, buffered DirectInput keyboard, or `SendInput` path so D-pad navigation does not move the character.
- 真三5菜单状态改为独立维护按住、按下和连发字段，修复一次短按跳两格的问题。
  SM5 now tracks held, pressed, and repeated fields independently, fixing two-step movement from one tap.
- 真三7从完整手柄补丁改为 Platform-only 输入保留方案，只扩展震动桥接。
  DW8 uses the platform-only input-preserving build and only receives the force-feedback bridge.
- 配置、安装、发布和验证文档统一采用中英文双语说明，并移除旧候选包和调试产物。
  Configuration, installation, release, and verification docs now use bilingual instructions, and stale candidate/debug artifacts are removed.

## 相对 `orochi/master` 的差异 / Differences from `orochi/master`

`orochi/master` 是 2026-11-14 的蛇魔Z单游戏实现。本地版本保留其蛇魔Z窗口和输入适配基础，并扩展为七个游戏配置、两个 DLL 构建、安装备份和测试工程。
`orochi/master` is the 2026-11-14 single-game Orochi Z implementation. The local version keeps the Orochi Z foundation and extends it to seven game profiles, two DLL variants, backup-aware installers, and a test suite.

- 远程基线没有 `controller_takeover`、2P独立动作位、全局键盘过滤或按游戏菜单路由。
  The remote baseline has no global takeover switch, independent P2 action words, shared-key filtering, or per-title menu routing.
- 远程基线没有真三3/4/5/6DX、战国2和真三7的专用模块，也没有第三方 SSM6 DX MOD 打包。
  The remote baseline has no dedicated SSM3/4/5/6 DX, SW2, or DW8 modules and no SSM6 DX third-party MOD bundle.
- 远程基线没有统一震动效果对象、DS4 USB/Bluetooth 报告、XInput 双马达输出或真三7原生力反馈回退。
  The remote baseline has no unified effect objects, DS4 USB/Bluetooth reports, XInput dual-motor output, or DW8 native-feedback fallback.
- 远程基线使用单个 `dinput8.dll` 与单一配置；本地发布使用 `build/dinput8.dll` 和 `build/dinput8-platform.dll`，按可执行文件选择配置。
  The remote baseline uses one `dinput8.dll` and one configuration. The local release uses full and platform-only builds selected by executable name.

## 验证 / Verification

本地发布前运行 `test.cmd`、`build.cmd`、`python release.py`，并检查每个子包可解压、DLL 哈希和配置文件存在。远程差异只记录功能，不把远程提交直接合并进本地发布分支。
Before release, run `test.cmd`, `build.cmd`, and `python release.py`, then verify every sub-zip, DLL hash, and configuration file. Remote differences are recorded here and are not merged directly into the local release branch.
