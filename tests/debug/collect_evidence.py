"""Read-only PE inspection. Shell/capstone evidence, not REA retained Evidence."""
from pathlib import Path
import hashlib, json, struct
import pefile, capstone
game = Path(r'E:\Games\Non-Steam\Musou Orochi Z')
out = Path(__file__).resolve().parent
records = []
for name in ['OROCHI_Z_TC.exe', 'dinput8.dll']:
    path = game / name
    p = pefile.PE(str(path))
    records.append(dict(file=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        machine=hex(p.FILE_HEADER.Machine), image_base=hex(p.OPTIONAL_HEADER.ImageBase),
        imports={e.dll.decode(): [i.name.decode() if i.name else i.ordinal for i in e.imports] for e in p.DIRECTORY_ENTRY_IMPORT}))
(out/'pe_identity.json').write_text(json.dumps(records, indent=2, ensure_ascii=False), encoding='utf-8')
p = pefile.PE(str(game/'OROCHI_Z_TC.exe'))
data = p.get_memory_mapped_image()
dis = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
ranges = [(0x5e4850,0xa4),(0x5e49e0,0x6d),(0x5e4b3f,0x48),(0x5e4c90,0x40),
          (0x5e53b7,0x40),(0x5e66a0,0xb3),(0x5e68d0,0x95),(0x5e696b,0x10),
          (0x5e6e5f,0x2b),(0x602710,6)]
lines=[]
for start,size in ranges:
    lines.append(f'\n; game VA {start:08x}')
    for x in dis.disasm(data[start-0x400000:start-0x400000+size],start):
        lines.append(f'{x.address:08x}  {x.bytes.hex():24} {x.mnemonic:10} {x.op_str}')
lines.append('\n; DIDATAFORMAT at 007573ec and first 36 DIOBJECTDATAFORMAT entries')
lines.append(data[0x3573ec:0x3573ec+24].hex())
for n in range(36):
    lines.append(str(tuple(hex(v) for v in struct.unpack_from('<IIII',data,0x201cd0+n*16))))
(out/'game_input.asm').write_text('\n'.join(lines),encoding='utf-8')
print('Saved pe_identity.json and game_input.asm')
