"""Build a test bundle without replacing/deleting any existing release package."""
from pathlib import Path
from io import BytesIO
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED
import hashlib
from release import GAMES

ROOT = Path(__file__).resolve().parent
VERSION = 'V1.0.5-rc2'
OUT = ROOT / '_rel' / f'MUSOUTweaksPC_{VERSION}.zip'

def add(archive, name, data):
    info = ZipInfo(name, (2026, 11, 19, 0, 0, 0))
    info.compress_type = ZIP_DEFLATED
    archive.writestr(info, data)

OUT.parent.mkdir(exist_ok=True)
with ZipFile(OUT, 'w', ZIP_DEFLATED) as total:
    add(total, 'FIX_STATUS.md', (ROOT / 'FIX_STATUS.md').read_bytes())
    add(total, 'CONFIGURATION.md', (ROOT / 'CONFIGURATION.md').read_bytes())
    for name, _cn, _en, dll, ini, extras, _game_version in GAMES:
        buffer = BytesIO()
        with ZipFile(buffer, 'w', ZIP_DEFLATED) as game:
            add(game, 'dinput8.dll', (ROOT / dll).read_bytes())
            add(game, ini, (ROOT / ini).read_bytes())
            for dst, src in extras:
                add(game, dst, (ROOT / src).read_bytes())
            if extras:
                add(game, 'THIRD_PARTY_MODS.md', (ROOT / 'THIRD_PARTY_MODS.md').read_bytes())
            add(game, 'FIX_STATUS.md', (ROOT / 'FIX_STATUS.md').read_bytes())
        add(total, f'{name}_{VERSION}.zip', buffer.getvalue())
with ZipFile(OUT) as archive:
    assert archive.testzip() is None
print(f'{OUT} ({OUT.stat().st_size} bytes)')
print('sha256', hashlib.sha256(OUT.read_bytes()).hexdigest())
