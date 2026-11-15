"""Package multi-game binaries, source code, and per-game configuration."""
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parent
VERSION = '2.16'
files = ['README.md','OrochiZPadX.ini','SSM3_Tweaks.ini','SSM4_Tweaks.ini',
         'SM2_Tweaks.ini','SSM7_Tweaks.ini','CONFIGURATION.md','MULTI_GAME.md',
         'SW2_DIAGNOSIS.md','install-targets.ps1','install-sw2.ps1','restore.ps1',
         'build.cmd','build-platform.cmd','package.py','.gitignore',
         'build/dinput8.dll','build/dinput8-platform.dll',
         'src/padfix.cpp','src/platform.inc','src/dinput8.def','src/dinput8-platform.def']
output = ROOT / f'MUSOUTweaksPC-{VERSION}-x86.zip'
for name in files:
    if not (ROOT/name).is_file():
        raise FileNotFoundError(name)
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
    for name in files:
        archive.write(ROOT/name, str(Path(name)).replace('\\','/'))
print(f'{output} ({output.stat().st_size} bytes)')
