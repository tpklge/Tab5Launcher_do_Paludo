from pathlib import Path
import importlib.util
import hashlib
import struct
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('upgrade',root/'tools/prepare_multiboot_upgrade.py'); module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
spec=importlib.util.spec_from_file_location('fixtures',root/'tests/firmware/fixtures.py'); fixtures=importlib.util.module_from_spec(spec); spec.loader.exec_module(fixtures)
def table(rows):
    out=b''.join(struct.pack('<HBBII16sI',0x50aa,*row) for row in rows)
    return out+b'\xeb\xeb'+b'\xff'*14+hashlib.md5(out).digest()
rows=[(0,0,0x20000,0x1f0000,b'launcher',0),(0,16,0x210000,0x800000,b'app0',0),(1,2,0x9000,0x6000,b'nvs',0)]
old=table(rows); rows[1]=(0,16,0x210000,0x400000,b'app0',0); rows.append((0,17,0x610000,0x400000,b'app1',0)); new=table(rows)
backup=bytearray(b'\xff'*0x1000000); backup[0x8000:0x8000+len(old)]=old; image=fixtures.image(); backup[0x210000:0x210000+len(image)]=image
assert module.preflight(backup,new)['app0_image_size']==len(image)
def rejected(blob,tab):
 try: module.preflight(blob,tab)
 except ValueError: return
 raise AssertionError('unsafe migration accepted')
rejected(backup[:-1],new)
bad=backup.copy(); bad[0x610000]=0; rejected(bad,new)
bad=backup.copy(); bad[0x210100]^=1; rejected(bad,new)
bad=bytearray(new); bad[12]^=1; rejected(backup,bad)
rows[2]=(1,2,0x9000,0x5000,b'nvs',0); rejected(backup,table(rows))
print('Multiboot preflight: image checksum/hash, dirty upper half, table integrity, full backup and preserved-data layout passed.')
