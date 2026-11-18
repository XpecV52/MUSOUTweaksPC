"""Build the public release bundle: one sub-zip per game, wrapped in one zip.

Unlike ``package.py`` (which packs the whole source tree for local self-testing),
this only ships what a player needs: the proxy DLL plus the matching ini. The
version scheme is the public one (``V1.0.x``), not the local 2.x iteration.
"""
from pathlib import Path
import hashlib
import zipfile

ROOT = Path(__file__).resolve().parent
VERSION = 'V1.0.4'
OUT = ROOT / '_rel'

# Fixed timestamp so the archives are byte-reproducible across runs.
STAMP = (2026, 11, 18, 0, 0, 0)

# name, chinese title, english title, dll, ini, extra files (dst, src), tested game version
GAMES = [
    ('MOZTweaks', '无双蛇魔Z', 'Musou Orochi Z',
     'build/dinput8.dll', 'OrochiZPadX.ini', [], 'V1.0.0.0'),
    ('SSM3Tweaks', '真三国无双3', 'Dynasty Warriors 4: Hyper',
     'build/dinput8.dll', 'SSM3_Tweaks.ini', [], '游侠繁体汉化 V0.1.1.0 (2.0.0.0)'),
    ('SSM4SPTweaks', '真三国无双4 Special', 'Dynasty Warriors 5 Special',
     'build/dinput8.dll', 'SSM4_Tweaks.ini', [], 'V1.0.0.0'),
    ('SSM5Tweaks', '真三国无双5', 'Dynasty Warriors 6',
     'build/dinput8.dll', 'SSM5_Tweaks.ini', [], 'V1.0.0.1'),
    ('SSM6DXTweaks', '真三国无双6 with 猛将传 DX', 'Dynasty Warriors 7 DX',
     'build/dinput8.dll', 'SSM6_Tweaks.ini',
     [('d3dx9_43.dll', 'supplemental/ssm6tc/d3dx9_43.dll'),
      ('mods/Fix_for_SM6.dll', 'supplemental/ssm6tc/mods/Fix_for_SM6.dll'),
      ('mods/Fix_for_SM6/enable.txt', 'supplemental/ssm6tc/mods/Fix_for_SM6/enable.txt')],
     'Steam V1.0.0.0'),
    ('SSM7wMTweaks', '真三国无双7 with 猛将传', 'Dynasty Warriors 8',
     'build/dinput8-platform.dll', 'SSM7_Tweaks.ini', [], 'Steam V1.0.4.0'),
    ('SM2Tweaks', '战国无双2', 'Sengoku Musou 2',
     'build/dinput8.dll', 'SM2_Tweaks.ini', [], 'V1.0.0.0'),
]

MODS_NOTE = """\
真三国无双6 with 猛将传 DX / Dynasty Warriors 7 DX —— 第三方组件说明
Third-party components for SSM6 DX

这些文件不是本补丁的一部分，单独授权，随包分发只为省事。
These files are not part of this patch, they carry their own licenses, and are
bundled only for convenience.

  d3dx9_43.dll        MOD 加载器 / mod loader (wxhwz). 没有它 mods\\ 不会加载。
  mods\\Fix_for_SM6.dll  修复过场/战斗中面部表情异常。
                      Fixes the broken facial expressions in cutscenes and battle.
  mods\\Fix_for_SM6\\enable.txt  加载开关。删掉后加载器会自己重建，
                     要禁用请把整个 Fix_for_SM6 目录挪出 mods\\。
                     Delete it and the loader recreates it; to disable the mod
                     move the whole Fix_for_SM6 folder out of mods\\ instead.

不需要这两个组件的话，只装 dinput8.dll + SSM6_Tweaks.ini 也能用。
If you do not want them, dinput8.dll + SSM6_Tweaks.ini alone still works.
"""

README = """\
MUSOUTweaksPC {version}
=========================

无双系列 / 战国无双系列 手柄 + 画面调整补丁（32 位 dinput8.dll 代理）
Gamepad and display tweaks for the Koei Musou ports (32-bit dinput8.dll proxy)

--------------------------------------------------------------------------
一、包内结构 / What is inside
--------------------------------------------------------------------------
每个游戏一个独立压缩包，先解压出自己那一个，再装。
One sub-zip per game. Extract only the one you need.

{table}

方括号里是补丁实际验证过的游戏版本，其他版本大概率也能用。
The bracketed value is the game build each patch was verified against; other
builds will most likely work too.

SSM7 用的是 platform-only 版本，文件名同样叫 dinput8.dll，别拿错。
SSM7 ships the platform-only build; inside its sub-zip the file is still named
dinput8.dll on purpose.

--------------------------------------------------------------------------
二、安装 / Install
--------------------------------------------------------------------------
1. 把子包里的 dinput8.dll 和对应的 .ini 复制到游戏 exe 所在目录
   （和游戏主程序同一个文件夹）。
   Copy dinput8.dll and the matching .ini into the folder that holds the game
   executable.
2. 启动游戏。ini 和 dll 必须同名放在一起，第一次运行会按 exe 名字自动挑默认值。
   Launch the game. Keep the ini next to the dll; defaults are picked from the
   executable name on first run.

ini 文件名可以改，但改完要保证和 exe 同目录；补丁只读自己名字对应的 ini
（dinput8.dll 读 dinput8.ini，没有则按游戏名找 *_Tweaks.ini）。
The ini name may be changed as long as it stays in that folder; the proxy looks
for dinput8.ini first, then the per-game *_Tweaks.ini.

--------------------------------------------------------------------------
三、卸载 / Uninstall
--------------------------------------------------------------------------
删掉 dinput8.dll 和 .ini 即可，不改注册表、不写系统目录。
Delete dinput8.dll and the ini. Nothing is written to the registry or to system
folders.

--------------------------------------------------------------------------
四、出问题 / Troubleshooting
--------------------------------------------------------------------------
先把 ini 里的 log 改成 1，再启动游戏，同目录会生成 MUSOUTweaksPC.log。
Set log=1 in the ini, run the game, and read MUSOUTweaksPC.log next to it.

各游戏单独说明见仓库：https://github.com/XpecV52/MUSOUTweaksPC
Per-game notes: https://github.com/XpecV52/MUSOUTweaksPC
"""


NOTES = """\
无双系列 / 战国无双系列 手柄 + 画面调整补丁（32 位 `dinput8.dll` 代理）。
Gamepad and display tweaks for the Koei Musou PC ports.

## 包内结构 / Contents

按游戏分类，每个游戏一个子包，装哪个解哪个。
One sub-zip per game — extract only the one you need:

| 子包 | 游戏 | 适配版本 | 安装文件 |
| --- | --- | --- | --- |
| `MOZTweaks` | 无双蛇魔Z / Musou Orochi Z | `V1.0.0.0` | `dinput8.dll` + `OrochiZPadX.ini` |
| `SSM3Tweaks` | 真三国无双3 / Dynasty Warriors 4: Hyper | 游侠繁体汉化 `V0.1.1.0`（`2.0.0.0`） | `dinput8.dll` + `SSM3_Tweaks.ini` |
| `SSM4SPTweaks` | 真三国无双4 Special / Dynasty Warriors 5 Special | `V1.0.0.0` | `dinput8.dll` + `SSM4_Tweaks.ini` |
| `SSM5Tweaks` | 真三国无双5 / Dynasty Warriors 6 | `V1.0.0.1` | `dinput8.dll` + `SSM5_Tweaks.ini` |
| `SSM6DXTweaks` | 真三国无双6 with 猛将传 DX / Dynasty Warriors 7 DX | Steam `V1.0.0.0` | `dinput8.dll` + `SSM6_Tweaks.ini` + `mods/` |
| `SSM7wMTweaks` | 真三国无双7 with 猛将传 / Dynasty Warriors 8 | Steam `V1.0.4.0` | `dinput8.dll`（platform-only）+ `SSM7_Tweaks.ini` |
| `SM2Tweaks` | 战国无双2 / Sengoku Musou 2 | `V1.0.0.0` | `dinput8.dll` + `SM2_Tweaks.ini` |

「适配版本」是补丁实际验证过的游戏版本。补丁按可执行文件名和内存特征码识别，其他版本大概率也能用；换了版本后某个功能失效的话，把 ini 里的 `log` 改成 `1` 跑一次，日志会写明哪一步没匹配上。
"Tested version" is the build each patch was verified against. Other builds will most likely work; if something stops working after an update, set `log=1` and the log says which step failed to match.

## 相对 V1.0.3 的变化 / Changes since V1.0.3

- **新增真三国无双3 小地图放大。** 这一作的小地图只有 104×128 像素（分屏时每个视口各一块），补丁在 D3D9 里把**每帧采样 410×440 地图图集的最大那个屏幕空间四边形**认作底图，复制顶点、以右上角为锚点等比放大后提交副本，默认 **160%**。地图上的单位点、玩家箭头、据点图标是各自单独提交的小四边形，会被匹配到所在视口的地图矩形、用同一套变换一起放大，底图与标记点始终同步；单人模式与上下分屏都生效。游戏自己的顶点缓冲不改，其他 HUD 不受影响。
- 新增小地图热键：**`Ctrl+Alt+F2` 放大 / `Ctrl+Alt+F3` 缩小**，每次 10%，范围 80–320%，按住可连调，新值立即写回 `minimap_scale`。新增配置项 `minimap_scale`、`minimap_hotkeys`、`minimap_surface_width` / `minimap_surface_height`（后两个是地图纹理尺寸指纹，换语言版本时可调）。
- **修复战国无双2 十字键不生效、以及十字键带动角色。** 该作菜单只认 `SendInput` 注入的键盘事件（既不轮询 DirectInput 键盘设备，也不认外部投递的窗口消息），所以 `dpad_arrow_keys` 对它改为 `2`。菜单光标与角色移动共用同一路键盘，补丁改为**只在菜单帧注入方向键**：在 D3D9 层统计每帧真实 3D 绘制数，低于 `menu_draw_threshold`（默认 `300`，实测菜单 0–1、战斗中暂停菜单 54、战斗 1524–2018）才注入，战斗中完全不注入，角色不会跟着走，左摇杆也完全不动。
- **战国无双2 帧率默认改为 60。** 这一作逻辑每渲染一帧推进一次，帧率就是游戏速度；之前按 65 锁帧虽然节流准确（`pace target=65 actual=64.99`），但整体快 8.3%。
- **修复战国无双2 锁帧完全没生效。** 该作的 D3D9 设备从未被挂上钩子（`Direct3DCreate9` 在 `dinput8.dll` 加载前就解析好了），晚挂钩线程又因为存在覆盖层就直接放弃。现在晚挂钩不再直接退出，会先给早期钩子宽限期，仍未接管才回退到堆扫描，并且只挂第一张匹配的设备 vtable。
- 新增诊断开关 `quad_probe`：把每帧提交的屏幕空间四边形与纹理尺寸写进日志，用于定位 HUD 元素。

## 相对 V1.0.2 的变化 / Changes since V1.0.2

- **修复无双蛇魔Z 在高分辨率下的整屏图层错乱。** 引擎的精灵坐标是 12.4 定点数，内部渲染高度超过 2048 就会溢出，补丁被迫同时启用「精灵 Y 回绕修补」和「全屏背景四边形深度写入修补」；两者叠加后背景会按 `z=0` 把后面所有 `z>0` 的 UI 和 3D 判死，只能靠关掉深度写入放行，而关掉之后过渡层、压暗层又全部透出来。症状是：选择故事/列传多出一层背景、兵营界面整屏发白、loading 只剩余底色、即时演算字幕消失。**`internal_render_width` / `internal_render_height` 默认改为跟随桌面分辨率**，配置说明里写明 2048 是红线。
- **修复无双蛇魔Z 的手柄确认键。** 选择故事、列传以及「2P 加入」弹窗只接受键盘确认，手柄原来完全无响应。现在 **B 键和 OPTION 键等同键盘 Enter**。该作既不轮询 DirectInput 键盘设备，也不认外部投递的窗口消息，所以改走 `SendInput`，由系统输入栈生成真实的键消息。新增 `pad_enter_buttons` / `pad_enter_mode` / `pad_enter_pulse_ms` 三个配置项。
- 新增 `orochi_depth_fix`：背景四边形深度写入的处理方式，仅在内部渲染高度超过 2048 时生效。

## 相对 V1.0.1 的变化 / Changes since V1.0.1

- **新增真三国无双5（`SM5_WIN.exe`）支持**，子包 `SSM5Tweaks`。该作的手柄列表按**产品名白名单**过滤，名字不在名单里的设备能被枚举但不会被选中，所以虚拟手柄默认报成 `Controller (XBOX 360 For Windows)`；用右摇杆转视角，因此默认暴露 6 轴（左摇杆 + 扳机 + 右摇杆）。
- **真三国无双5 的十字键不接左摇杆。** 只作为 POV 方向帽和四个独立按钮交给游戏（可单独绑键、不会带动角色），菜单光标改在**游戏自己的键盘设备**上注入方向键事件。按住方向超过 0.4 秒后自动连发，光标连续移动。
- **真三国无双5 的无边框全屏修好了。** 该作只静态导入 `MessageBoxA`，`CreateWindowExA` 的 IAT 钩子打不上，原来的窗口识别完全失效；现在改为按注册类名 `SangokuMusou5PC` 轮询接管，找不到时还会兜底接管进程唯一的可见大窗口。
- 新增 `dpad_menu_repeat_ms` / `dpad_menu_repeat_delay_ms` 长按连发（默认关闭，只有真三5 默认开启）。

## 相对 V1.0.0 的变化 / Changes since V1.0.0

- **修复真三国无双6 DX 启动后画面异常、视野错位。** `native_resolution` 与 `scale_to_fill` 默认改回 `0`，画面交给无边框窗口拉伸。开启原生分辨率时引擎只往放大后的后备缓冲区左上角绘制，右侧和底部是黑的，所以这一项对该作必须保持关闭。
- **真三国无双6 DX 十字键改走游戏键盘设备。** 直接在游戏缓冲模式的键盘设备上注入方向键事件，不再桥接左摇杆 —— 菜单能选，角色也不会跟着走。
- **移除第三方 `resolution_for_SM6TC` 分辨率 MOD。** 它只改引擎内部渲染分辨率、不改逻辑分辨率，2D 层和 3D 层错位，且与无边框缩放冲突。保留 `d3dx9_43.dll`（MOD 加载器）和 `Fix_for_SM6.dll`（修复面部表情）。
- **新增 `frame_capture` 诊断项。** 置 `1` 后游戏内出现标记文件即导出当前帧 BMP，方便排查画面问题。
- 子包命名拼写修正（`Twaks` → `Tweaks`）。

## 安装 / Install

把子包里的 `dinput8.dll` 和对应 `.ini` 复制到游戏 exe 所在目录，启动游戏即可。
卸载删掉这两个文件，不写注册表、不改系统目录。

Copy `dinput8.dll` and the matching `.ini` into the folder that holds the game
executable. To uninstall, delete those two files — nothing else is touched.

排障：把 ini 里的 `log` 改成 `1`，游戏目录会生成 `MUSOUTweaksPC.log`。
Troubleshooting: set `log=1` and read `MUSOUTweaksPC.log` next to the game.
"""


def add(archive, name, data):
    info = zipfile.ZipInfo(name, STAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    archive.writestr(info, data)


def build_subzip(name, dll, ini, extras):
    buffer = ROOT / '_rel' / f'{name}_{VERSION}.zip'
    with zipfile.ZipFile(buffer, 'w') as archive:
        add(archive, 'dinput8.dll', (ROOT / dll).read_bytes())
        add(archive, Path(ini).name, (ROOT / ini).read_bytes())
        for dst, src in extras:
            add(archive, dst, (ROOT / src).read_bytes())
        if extras:
            add(archive, 'THIRD_PARTY_MODS.txt', MODS_NOTE.replace('\n', '\r\n').encode('utf-8-sig'))
    return buffer


def main():
    OUT.mkdir(exist_ok=True)
    for stale in OUT.glob('*.zip'):
        if stale.name != f'MUSOUTweaksPC_{VERSION}.zip':
            stale.unlink()

    subs = []
    for name, _, _, dll, ini, extras, _ver in GAMES:
        subs.append(build_subzip(name, dll, ini, extras))

    rows = '\n'.join(
        f'  {name}_{VERSION}.zip  {cn} / {en}  [{ver}]'
        for name, cn, en, _dll, _ini, _extras, ver in GAMES)
    text = README.format(version=VERSION, table=rows).replace('\n', '\r\n')

    total = OUT / f'MUSOUTweaksPC_{VERSION}.zip'
    with zipfile.ZipFile(total, 'w') as archive:
        add(archive, 'README.txt', text.encode('utf-8-sig'))
        for path in subs:
            add(archive, path.name, path.read_bytes())

    digest = hashlib.sha256(total.read_bytes()).hexdigest()
    notes = OUT / f'NOTES_{VERSION}.md'
    notes.write_text(NOTES, encoding='utf-8')
    print(f'{total.name}  {total.stat().st_size} bytes')
    print(f'sha256 {digest}')
    print(f'notes  {notes.name}')
    for path in subs:
        print(f'  - {path.name}  {path.stat().st_size} bytes')


if __name__ == '__main__':
    main()
