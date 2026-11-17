# 发布产物

面向玩家的发布包**只放在 GitHub Releases**，仓库里不再保存任何 zip。

- 发布页：<https://github.com/XpecV52/MUSOUTweaksPC/releases>
- 发布包版本号与本地迭代号（`2.x`）是两套编号：本地 `package.py` 打的是开发自测包（`MUSOUTweaksPC-<2.x>-x86.zip`，不入库）；对外发布包由 `release.py` 生成，按游戏拆分后打成整包，版本形如 `V1.0.1`。

## GitHub Releases 记录

| Tag | 资产 | 大小 (B) | SHA-256 |
| --- | --- | ---: | --- |
| `v1.0.0` | `MUSOUTweaksPC_V1.0.0.zip` | 432822 | 见发布页 |
| `v1.0.1` | `MUSOUTweaksPC_V1.0.1.zip` | 1091191 | `a03d7b92ecd4d5292b52c93d3ec03ae3b627f83caf22b6b8b15a0b882f078648` |
| `v1.0.2` | `MUSOUTweaksPC_V1.0.2.zip` | 1245363 | `191254f769cac0ea19447271935d4e11d0e217679e6f7ffb8864d49c3963de97` |

`v1.0.1` 对应内部构建 **2.45**、`v1.0.2` 对应 **2.46**、`v1.0.3` 对应 **2.47**（dll 日志横幅里打印的是构建号 `MUSOUTweaksPC 2.47`，不是发布号）。

**2.47 尚未发布**，本地构建已可用。主要内容：

- 无双蛇魔 Z：`internal_render_width` / `internal_render_height` 默认改为跟随桌面分辨率。引擎精灵坐标是 12.4 定点数，高度超过 2048 即溢出，补丁会被迫启用精灵 Y 修补与背景深度写入修补，两者叠加会破坏图层顺序（选择故事/列传多出背景层、兵营整屏发白、loading 只剩底色、即时演算字幕消失）。
- 无双蛇魔 Z：新增 `orochi_depth_fix`（0/1/2）。
- 无双蛇魔 Z：手柄 B / OPTION 等同键盘 Enter，新增 `pad_enter_buttons` / `pad_enter_mode` / `pad_enter_pulse_ms`。该作不读 DirectInput 键盘设备、也不认 `PostMessage`，只有 `SendInput` 生成的系统级键消息能被识别。

## 支持的游戏版本

补丁按可执行文件名和内存特征码识别，下表是**实际验证过**的游戏版本；其他版本大概率也能用，失效时把 ini 的 `log` 改成 `1` 看日志。

| 游戏 | 适配版本 | 可执行文件 | 子包 |
| --- | --- | --- | --- |
| 真三国无双3 / Dynasty Warriors 4: Hyper | 游侠繁体汉化 `V0.1.1.0`（`2.0.0.0`） | `ShinSangokumusou3.exe` | `SSM3Tweaks` |
| 真三国无双4 Special / DW5 Special | `V1.0.0.0` | `SM4_WIN.exe` / `SS4*.exe` | `SSM4SPTweaks` |
| 真三国无双5 / Dynasty Warriors 6 | `V1.0.0.1` | `SM5_WIN.exe` | `SSM5Tweaks` |
| 真三国无双6 with 猛将传 DX / DW7 DX | Steam `V1.0.0.0` | `SM6TC.exe` / `SM6EN.exe` / `SM6JP.exe` | `SSM6DXTweaks` |
| 真三国无双7 with 猛将传 / DW8 | Steam `V1.0.4.0` | `Launch.exe` | `SSM7wMTweaks` |
| 战国无双2 / Sengoku Musou 2 | `V1.0.0.0` | `SW2.exe` | `SM2Tweaks` |
| 无双蛇魔Z / Musou Orochi Z | `V1.0.0.0` | `OROCHI_Z_TC.exe` | `MOZTweaks` |

发布流程：

```powershell
python release.py                  # 生成 _rel\MUSOUTweaksPC_V1.0.3.zip + 七个子包 + NOTES_V1.0.3.md
python publish.py V1.0.3 --notes _rel\NOTES_V1.0.3.md
```

`publish.py` 走 `api.github.com`（本机 `gh` 不可用），token 取自 git 凭据助手；同名 asset 会先删再传，所以可以反复跑。

### V1.0.2 内容

新增真三国无双5；整包内按游戏名分类，每个游戏一个子包：

| 子包 | 大小 (B) | 游戏 | 适配版本 | 安装文件 |
| --- | ---: | --- | --- | --- |
| `MOZTweaks_V1.0.2.zip` | 140167 | 无双蛇魔Z / Musou Orochi Z | `V1.0.0.0` | `dinput8.dll` + `OrochiZPadX.ini` |
| `SSM3Tweaks_V1.0.2.zip` | 139069 | 真三国无双3 / Dynasty Warriors 4: Hyper | 游侠繁体汉化 `V0.1.1.0`（`2.0.0.0`） | `dinput8.dll` + `SSM3_Tweaks.ini` |
| `SSM4SPTweaks_V1.0.2.zip` | 139069 | 真三国无双4 Special / Dynasty Warriors 5 Special | `V1.0.0.0` | `dinput8.dll` + `SSM4_Tweaks.ini` |
| `SSM5Tweaks_V1.0.2.zip` | 141388 | 真三国无双5 / Dynasty Warriors 6 | `V1.0.0.1` | `dinput8.dll` + `SSM5_Tweaks.ini` |
| `SSM6DXTweaks_V1.0.2.zip` | 428736 | 真三国无双6 with 猛将传 DX / Dynasty Warriors 7 DX | Steam `V1.0.0.0` | `dinput8.dll` + `SSM6_Tweaks.ini`（另附 `d3dx9_43.dll` + `mods/Fix_for_SM6*`） |
| `SSM7wMTweaks_V1.0.2.zip` | 115723 | 真三国无双7 with 猛将传 / Dynasty Warriors 8 | Steam `V1.0.4.0` | `dinput8.dll`（platform-only 版）+ `SSM7_Tweaks.ini` |
| `SM2Tweaks_V1.0.2.zip` | 138730 | 战国无双2 / Sengoku Musou 2 | `V1.0.0.0` | `dinput8.dll` + `SM2_Tweaks.ini` |

V1.0.2 主要变化：新增真三5（手柄白名单设备名、6 轴右摇杆、无边框按类名兜底）；十字键不再桥接左摇杆，菜单光标改走游戏自己的键盘设备；新增长按连发。详见 `_rel/NOTES_V1.0.2.md`。

### V1.0.1 内容

整包内按游戏名分类，每个游戏一个子包：

| 子包 | 大小 (B) | 游戏 | 安装文件 |
| --- | ---: | --- | --- |
| `MOZTweaks_V1.0.1.zip` | 137917 | 无双蛇魔Z / Musou Orochi Z | `dinput8.dll` + `OrochiZPadX.ini` |
| `SSM3Tweaks_V1.0.1.zip` | 136819 | 真三国无双3 / Dynasty Warriors 4: Hyper | `dinput8.dll` + `SSM3_Tweaks.ini` |
| `SSM4SPTweaks_V1.0.1.zip` | 136819 | 真三国无双4 Special / Dynasty Warriors 5 Special | `dinput8.dll` + `SSM4_Tweaks.ini` |
| `SSM6DXTweaks_V1.0.1.zip` | 426486 | 真三国无双6 with 猛将传 DX / Dynasty Warriors 7 DX | `dinput8.dll` + `SSM6_Tweaks.ini`（另附 `d3dx9_43.dll` + `mods/Fix_for_SM6*`） |
| `SSM7wMTweaks_V1.0.1.zip` | 114524 | 真三国无双7 with 猛将传 / Dynasty Warriors 8 | `dinput8.dll`（platform-only 版）+ `SSM7_Tweaks.ini` |
| `SM2Tweaks_V1.0.1.zip` | 136480 | 战国无双2 / Sengoku Musou 2 | `dinput8.dll` + `SM2_Tweaks.ini` |

整包根目录另有 `README.txt`（中英双语安装/卸载/排障说明）。

子包命名用 `Tweaks`；`v1.0.0` 里是 `Twaks`（拼写错误），`v1.0.1` 起统一改正。

## 已从仓库移除

- 全部 `MUSOUTweaksPC-<2.x>-x86.zip` / `OrochiZPadX-<x>-x86.zip` 本地发布包。源码都在 git 历史里，需要时按 `package.py` 重新打包。
- `supplemental/ssm6tc/mods/resolution_for_SM6TC.dll` 及其配置：**不兼容**。它把引擎内部渲染分辨率改成桌面尺寸，而游戏的逻辑分辨率不变，导致 3D 与 2D 层错位（表现为视野异常），且与无边框缩放路径冲突。保留的第三方组件只有 `d3dx9_43.dll`（MOD 加载器）和 `Fix_for_SM6.dll`（修复面部表情）。

## 清理的中间产物

- `analysis/`：调试期抓帧（单张 15 MB）、界面截图、一次性 PE/窗口分析 JSON。调试脚本已移到 `tests/`。
- `build/`：截图、`.obj`、`.map`、运行日志。只保留 `dinput8.dll` / `dinput8-platform.dll`（打包脚本直接读这两个文件）。
- 根目录：`*.obj` / `*.exp` / `*.lib` 等链接中间件。

`.gitignore` 忽略 `*.zip`，所以任何打包产物都不会被误提交。
