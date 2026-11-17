# 发布产物

面向玩家的发布包**只放在 GitHub Releases**，仓库里不再保存任何 zip。

- 发布页：<https://github.com/XpecV52/MUSOUTweaksPC/releases>
- 发布包版本号与本地迭代号（`2.x`）是两套编号：本地 `package.py` 打的是开发自测包（`MUSOUTweaksPC-<2.x>-x86.zip`，不入库）；对外发布包由 `release.py` 生成，按游戏拆分后打成整包，版本形如 `V1.0.1`。

## GitHub Releases 记录

| Tag | 资产 | 大小 (B) | SHA-256 |
| --- | --- | ---: | --- |
| `v1.0.0` | `MUSOUTweaksPC_V1.0.0.zip` | 432822 | 见发布页 |
| `v1.0.1` | `MUSOUTweaksPC_V1.0.1.zip` | 1090964 | `06d7e5e9ad530cd2b64c8a38d29be549a0e469f348dacbca40308e6bb65d26f5` |

`v1.0.1` 对应内部构建 **2.45**（dll 日志横幅里仍打印 `MUSOUTweaksPC 2.45`，那是构建号，不是发布号）。

发布流程：

```powershell
python release.py                  # 生成 _rel\MUSOUTweaksPC_V1.0.1.zip + 六个子包 + NOTES_V1.0.1.md
python publish.py V1.0.1 --notes _rel\NOTES_V1.0.1.md
```

`publish.py` 走 `api.github.com`（本机 `gh` 不可用），token 取自 git 凭据助手；同名 asset 会先删再传，所以可以反复跑。

### V1.0.1 内容

整包内按游戏名分类，每个游戏一个子包：

| 子包 | 大小 (B) | 游戏 | 安装文件 |
| --- | ---: | --- | --- |
| `MOZTweaks_V1.0.1.zip` | 137917 | 无双蛇魔Z / Musou Orochi Z | `dinput8.dll` + `OrochiZPadX.ini` |
| `SSM3Tweaks_V1.0.1.zip` | 136819 | 真三国无双3 / Dynasty Warriors 4: Hyper | `dinput8.dll` + `SSM3_Tweaks.ini` |
| `SSM4SPTweaks_V1.0.1.zip` | 136819 | 真三国无双4 Special / Dynasty Warriors 5 Special | `dinput8.dll` + `SSM4_Tweaks.ini` |
| `SSM6DXTweaks_V1.0.1.zip` | 426260 | 真三国无双6 with 猛将传 DX / Dynasty Warriors 7 DX | `dinput8.dll` + `SSM6_Tweaks.ini`（另附 `d3dx9_43.dll` + `mods/Fix_for_SM6*`） |
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
