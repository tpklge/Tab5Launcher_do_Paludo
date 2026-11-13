"""Strict parser regressions against synthetic/corrupted images and the real build."""
from pathlib import Path
import ctypes as C
import hashlib
import os
import random
import struct
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
from fixtures import image
class Info(C.Structure):
    _fields_=[('size',C.c_uint32),('chip',C.c_uint16),('min',C.c_uint16),('max',C.c_uint16),
              ('segments',C.c_uint8),('hash',C.c_bool),('project',C.c_char*33),('version',C.c_char*33),('idf',C.c_char*33)]
READ=C.CFUNCTYPE(C.c_bool,C.c_void_p,C.c_uint32,C.c_void_p,C.c_size_t)
HASH=C.CFUNCTYPE(C.c_bool,C.c_void_p,C.c_uint32,C.POINTER(C.c_ubyte))
with tempfile.TemporaryDirectory() as tmp:
    library=Path(tmp)/'parser.so'
    subprocess.run(['cc','-shared','-fPIC','-O1','-g','-fsanitize=undefined',
        '-I',str(ROOT/'components/firmware/include'),str(ROOT/'components/firmware/firmware_parser.c'),'-o',str(library)],check=True)
    lib=C.CDLL(str(library)); fn=lib.firmware_parse
    fn.argtypes=[C.c_void_p,READ,HASH,C.c_uint32,C.c_uint16,C.POINTER(Info),C.c_char_p,C.c_size_t]; fn.restype=C.c_bool
    chip_fn=lib.firmware_parse_chip;chip_fn.argtypes=[C.c_void_p,READ,HASH,C.c_uint32,C.c_uint16,C.c_uint16,C.POINTER(Info),C.c_char_p,C.c_size_t];chip_fn.restype=C.c_bool
    def parse(blob,revision=103,chip=None):
        blob=bytes(blob)
        @READ
        def read(ctx,off,out,n):
            if off+n>len(blob): return False
            C.memmove(out,blob[off:off+n],n); return True
        @HASH
        def digest(ctx,length,expected): return hashlib.sha256(blob[:length]).digest()==bytes(expected[:32])
        info=Info(); error=C.create_string_buffer(256)
        ok=fn(None,read,digest,len(blob),revision,C.byref(info),error,256) if chip is None else chip_fn(None,read,digest,len(blob),chip,revision,C.byref(info),error,256)
        return ok, info, error.value
    assert parse(image(chip=0x0d),revision=65535,chip=0x0d)[0]
    assert not parse(image(chip=0x12),revision=65535,chip=0x0d)[0]
    c6=ROOT/'sdcard/launcher/c6/network_adapter.bin'
    if c6.exists():
        ok,info,error=parse(c6.read_bytes(),revision=65535,chip=0x0d);assert ok,error;assert info.project==b'network_adapter' and info.version==b'2.13.1' and info.hash
    good=image(); assert parse(good)[0]
    rom=bytearray(good);struct.pack_into('<I',rom,4,0x4fc00000);rom[-32:]=hashlib.sha256(rom[:-32]).digest();assert parse(rom)[0]
    struct.pack_into('<I',rom,4,0x4fc20000);rom[-32:]=hashlib.sha256(rom[:-32]).digest();assert not parse(rom)[0]
    assert parse(good)[1].project==b'TestApp'
    assert not parse(image(chip=0x10))[0] # H2 is NOT P4
    assert not parse(image(revision=200))[0]
    assert not parse(image(maximum=102))[0]
    for n in (0,1,23,32,287,len(good)-1): assert not parse(good[:n])[0]
    for offset in (0,1,12,23,31,32,300,len(good)-1,len(good)-33):
        bad=bytearray(good); bad[offset]^=0x80; assert not parse(bad)[0],offset
    assert not parse(good+b'\xff'*16)[0]
    bad=bytearray(good); struct.pack_into('<I',bad,28,0xfffffffc); assert not parse(bad)[0]
    bad=bytearray(good); bad[1]=17; assert not parse(bad)[0]
    random.seed(42)
    for _ in range(1500):
        bad=bytearray(good)
        for _ in range(random.randint(1,8)): bad[random.randrange(len(bad))]^=random.randint(1,255)
        parse(bad)
    real=ROOT/'build/tab5_launcher.bin'
    if real.exists():
        ok,info,error=parse(real.read_bytes()); assert ok,error
        print(f'Real build parsed: {info.project.decode()}, {info.size} bytes, P4 chip={info.chip:#x}, SHA256 verified={info.hash}')
print('Firmware parser: format/chip/revision/bounds/checksum/hash and 1500 mutation cases passed (UBSan).')
