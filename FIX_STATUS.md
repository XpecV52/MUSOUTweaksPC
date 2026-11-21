# V1.0.5 发布状态与验证边界 / V1.0.5 Release Status and Verification Boundaries

## V1.0.5 发布整理 / Release cleanup

- 已将当前源码、两个 DLL 构建、七个游戏配置和必要第三方组件整理为正式 `MUSOUTweaksPC_V1.0.5.zip`。
- 已清理旧的候选压缩包、内存转储、截图、探测程序和临时构建目录。源码测试脚本仍保留，便于复现构建。
- `controller_takeover=0` 已通过回归检查，新增的1P/2P菜单接管、公共键盘过滤和临时动作表交换均不启用。
- `REMOTE_DIFFS.md` 单独记录本地分支相对 `origin/main` 与 `orochi/master` 的新增和修改功能。
- 当前发布仍保留“需要实机确认”的项目清单，自动化测试通过不等于所有实体手柄和全部游戏菜单均已现场验收。

- The current source, two DLL builds, seven game configs, and required third-party files are packaged as `MUSOUTweaksPC_V1.0.5.zip`.
- Old candidate archives, memory dumps, screenshots, probes, and temporary build directories are removed. Source tests remain for reproducible builds.
- Regression checks confirm that `controller_takeover=0` disables the added P1/P2 menu takeover, shared-key filtering, and temporary action-map swaps.
- `REMOTE_DIFFS.md` records added and modified functionality relative to `origin/main` and `orochi/master`.
- The release keeps the list of items requiring real-game verification. Passing automated tests does not claim every physical controller and menu path was tested live.

## 2026-11-20 rc4 后续修复

- 真三5绑定页不再隐藏菜单帧的 POV 和十字键按钮，标准 DirectInput 按钮对象按物理偏移解析。真三5双人菜单保留原生 TAB/J/I/空格/SHIFT 动作位。
- 真三6DX子菜单统一写入2P动作记录，避免子对象编号变化时把 TAB/SHIFT 等原生动作写入1P。
- 战国2选人页在2P加入后立即允许确认/返回，准备页仍等待1P准备状态。
- 蛇魔Z保留1P原生手柄 A/B 菜单状态，并允许非零玩家菜单回调走2P键盘或第二手柄来源。

- 真三3/4公共菜单的1P手柄接管不再依赖2P已加入。单人时B确认、A返回直接写入1P动作表，2P加入后仍由2P键盘WASD控制自己的光标。
- 真三3/4双人战斗采样期间隔离1P键盘绑定和2P手柄绑定，避免2P攻击串到1P；采样结束恢复原始配置表。
- 真三3默认关闭D-pad到左摇杆镜像，使用已验证的原生菜单D-pad映射，避免非战斗页面一次按键响应两次。
- 蛇魔Z选人和准备页面的按下回调也会刷新输入状态，避免首个按键沿用上一帧空状态。
- 所有无边框窗口清除WS_EX_NOACTIVATE并对主窗口返回MA_ACTIVATE，任务栏点击可以重新取得焦点。
- 真三5、真三6DX、战国2、蛇魔Z的2P菜单回写只覆盖方向/确认/返回，保留原生2P动作位，`TAB/J/I/空格/SHIFT` 等键在选人、准备和菜单页继续送达2P。
- 战国2选人不再依赖固定光标数值；2P加入后使用WASD，准备页在1P准备状态切换后接收2P输入，保留原生加入动作。
- 真三6DX的 `Ctrl+Alt+PageUp/PageDown` 镜头钩子改为允许通过 `VirtualProtect` 写入只读IAT，修复镜头快捷键未安装的问题。
- 真三5、真三6DX、战国2、蛇魔Z的DirectInput菜单与战斗输入继续使用1P手柄/2P键盘归属。真三7保留Platform-only原生输入归属，只增加震动桥接。
- 真三7原生力反馈成功但无实际马达输出时也会切换到统一的XInput/HID桥接；Xbox、DS4、DualSense均走对应输出路径。
- 本次最新构建只自动替换真三3、真三4、真三5和蛇魔Z四个游戏目录中的旧DLL。每个目录都保留本次 `MUSOUTweaksPC-backup-20261121-*` 回滚目录，真三6DX、战国2和真三7本次未部署，安装报告位于 `build/installed-upgrade-report.json`。

## 2.53-rc4 双人菜单与震动

- 真三3、真三4的 2P 重新选人会识别已有的 1P 选择。真三5、战国2、蛇魔Z使用各自的原生选人和准备状态路由输入。2P 用 WASD 移动，在游戏允许 2P 确认时使用 Enter 或 L、Esc 或 K。
- 真三5在分屏战斗采样时隔离 1P 手柄和 2P 键盘，游戏保存的键位保持原值。
- 真三7保留Platform-only原生键盘和手柄归属，仅扩展DirectInput力反馈。若原生力反馈创建成功但未驱动马达，补丁会将请求送到XInput/HID桥接。Xbox、DS4、DualSense使用统一双马达输出路径。
- 真三6DX的选人和准备路由适配本机繁中 `SM6TC.exe`，并保留2P原生附加动作位。`Ctrl+Alt+PageUp/PageDown`镜头快捷键通过DX导入表钩子安装。

## 2.53-rc3 双人输入归属

真三3、真三4的标准选人和战前准备使用游戏的2P加入、角色选择阶段和准备标志。1P十字键、摇杆和B/A进入1P动作表。2P使用WASD，只有游戏在双人选人中切到2P或确认1P已经准备后才接收Enter/Esc。共享菜单只接受1P，原生加入键分别为F1和F12。战斗中已存在的反向手柄/键盘配置仅在输入采样期间交换。

也检查了其余适配作品的输入路径。蛇魔Z的选人也会复制公共菜单输入，其阶段和动作位已有静态证据，战斗映射和系统按键通道仍需要单独验证。真三5、6和战国2的本地可执行文件保护了对应代码，当前证据只有键盘输送通道，尚不足以实现原生加入/准备判定。这四作仍用原行为。真三7继续使用原生手柄支持。

工程原先由 `src/padfix.cpp` 实现单个虚拟 DirectInput 手柄，`src/platform.inc` 负责窗口/D3D9/游戏特征码补丁。真三7使用 platform-only DLL，保留原生控制器。原有力反馈接口统一返回不支持，双人也没有独立的虚拟设备。

## 2.53-rc2：修正1P确认/返回被过滤

用户实测真三3方向和十字键正常，但B/A不生效。rc1将真三3默认分配到SendInput通道；DirectInput键盘过滤又清除Enter/Esc，且只在另一种通道补回手柄状态，导致原生A/B和替代按键同时被屏蔽。

rc2将真三3、真三4和其他DirectInput菜单作品默认设为游戏键盘设备通道，只有已知需系统输入的蛇魔Z/战国2保留SendInput。所有通道均记录手柄按键的独立状态与转换，过滤键盘重复事件后补回手柄按下/松开；仍由1P接管、B确认/A返回，2P不接管公共菜单。

新增真实系统DirectInput键盘集成检查，遍历B/A、状态读取/缓冲读取及两个通道。先复现了“过滤后手柄确认状态为空”的失败，再验证修复。共231项检查通过（148输入、20真实COM/D3D9/窗口、63回归）。完整游戏菜单链路仍需实机复测。

## 本次代码调整

- 战国2：首次方向短按之后先等400ms，再开始连发；松开与失焦清除计时器。SendInput方向注入使用原始十字键状态，避免再叠加一次按钮连发节拍。
- 其他补丁：键盘方向注入生效时不再向同一路输出重复的虚拟方向按钮/POV，也不再同时修改原生菜单方向表。关闭重复轴不影响右摇杆。
- 真三5：`trigger_axes=0` 保留LT/RT按钮，将Z/Rz轴保持松开，消除按钮绑定和原生轴动作的双重输出。是否该游戏所有版本均通过轴触发地图仍需实机验证。
- 所有INI新增 `[input] controller_takeover`，默认1。识别菜单后，仅已连接的1P手柄B/A生成Enter/Esc；2P不生成共享按键，断开1P不会提升2P。没有1P手柄时键盘仍能确认/返回。
- 可选第二个XInput虚拟手柄拥有独立GUID和状态，2P在战斗和当前菜单存在2P光标时输出到2P动作位；原生DS4与DualSense也可作为1P震动来源。
- DirectInput力反馈实现：能力与效果枚举、恒力/周期/斜坡效果、马达强度、时长、增益、停止/暂停、断连与失焦清理。真三7原生效果成功时也会转接到XInput/HID输出。
- 无边框：保留激活时的Z序及 `SWP_NOZORDER`，不再把每个激活请求强制改为 `HWND_NOTOPMOST`；仍约束窗口尺寸并拒绝永久置顶。

## 已验证

`test.cmd` 编译并运行原有输入测试、真实COM/D3D9/窗口测试，以及新增使用模拟时间和按键注入捕获的回归测试。模拟输入不会向桌面实际发送按键。具体计数见执行输出。

新增检查覆盖短按/长按、LT按钮与轴分离、菜单B到Enter单次转换、缓冲读取peek、1P/2P隔离、1P断连不提升2P、效果COM生命周期、马达归属/时长/暂停/停止，以及激活时保留窗口Z序。

## 尚未通过实机验收

- 各游戏从标题、设置、选人到暂停、结算、2P加入等完整菜单链路。菜单识别仍是绘制计数启发式，只有战国2有既有实测阈值；复杂3D菜单或低绘制量战场可能误判。
- 各游戏战斗事件是否确实产生正确震动请求，以及实体Xbox、DS4、DualSense马达的实际表现。
- 真三5 LT召马时地图显示是否完全不再切换，需要确认实际游戏绑定使用按钮而不是旧轴绑定。
- 所有游戏与Steam覆盖层共存时的实际任务栏切回、Alt+Tab、最小化/恢复。
- 真三4独立包与Parsec远程会话的联合测试。

因此发布包会保留上述验证边界说明。每次本地更新都会先备份旧DLL和配置，自动替换现有补丁并校验DLL哈希。

## 真三4独立分支

`codex/ssm4-borderless-only` 位于相邻工作目录 `../orochiz_ssm4_borderless`，基于原main单独制作。使用 `PADFIX_WINDOW_ONLY` 编译，DirectInput8Create直接转发系统原接口，配置不能重新启用设备独占修改。输入、震动、设备枚举均保留原驱动路径，只输出独立ZIP。
