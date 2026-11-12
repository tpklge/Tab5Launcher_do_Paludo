from pathlib import Path
import ctypes as C,hashlib,struct,tempfile,subprocess
root=Path(__file__).resolve().parents[2]
MD5=C.CFUNCTYPE(C.c_bool,C.POINTER(C.c_ubyte),C.c_size_t,C.POINTER(C.c_ubyte))
@MD5
def digest(b,n,out):
 value=hashlib.md5(bytes(b[:n])).digest(); C.memmove(out,value,16); return True
with tempfile.TemporaryDirectory() as tmp:
 lib=Path(tmp)/'layout.so'
 subprocess.run(['cc','-shared','-fPIC','-fsanitize=undefined','-I',str(root/'components/partitions_mgr/include'),str(root/'components/partitions_mgr/layout_model.c'),'-o',str(lib)],check=True)
 dll=C.CDLL(str(lib));fn=dll.layout_build_big; fn.argtypes=[C.c_void_p,C.c_void_p,MD5]; fn.restype=C.c_bool
 source=(root/'tests/fixtures/legacy-partition-table.bin').read_bytes().ljust(4096,b'\xff'); out=C.create_string_buffer(4096)
 assert fn(source,out,digest)
 def rows(b):
  result={}
  for off in range(0,3072,32):
   row=struct.unpack_from('<HBBII16sI',b,off)
   if row[0]==0xebeb: assert hashlib.md5(b[:off]).digest()==b[off+16:off+32]; break
   assert row[0]==0x50aa; result[row[5].split(b'\0')[0]]=row
  return result
 old=rows(source); new=rows(out.raw); assert b'app1' not in new and new[b'app0'][4]==0x800000
 for key,value in old.items():
  if key not in (b'app0',b'app1'): assert new[key]==value
 for at in (0,12,32,64,128):
  bad=bytearray(source); bad[at]^=0x80; assert not fn(bytes(bad),out,digest)
 assert not fn(out.raw,out,digest) # already big, refused
 print('Layout: checked 2x4 -> 1x8 MB, all data/launcher regions preserved, bad checksums and unsupported profile refused (UBSan).')

 profile=dll.layout_build_profile;profile.argtypes=[C.c_void_p,C.c_void_p,C.POINTER(C.c_uint32),C.c_uint,MD5];profile.restype=C.c_bool
 for sizes in [(8,), (4,4),(3,3,2),(2,2,2,2),(1,7)]:
  units=(C.c_uint32*len(sizes))(*[v*1048576 for v in sizes]);assert profile(source,out,units,len(sizes),digest)
  generated=rows(out.raw)
  for key,value in old.items():
   if key not in (b'app0',b'app1'):assert generated[key]==value
  assert sum(v[4] for k,v in generated.items() if k.startswith(b'app'))==0x800000
  for nextsizes in [(8,),(4,4),(2,2,2,2)]:
   nextunits=(C.c_uint32*len(nextsizes))(*[v*1048576 for v in nextsizes]);other=C.create_string_buffer(4096);assert profile(out.raw,other,nextunits,len(nextsizes),digest)
 for sizes in [(9,), (4,3),(0,8),(512,512),()]:
  units=(C.c_uint32*len(sizes))(*[v*1048576 for v in sizes]);assert not profile(source,out,units,len(sizes),digest)
 print('Visual profiles: 1–4 slots, repeat layout transitions, exact preservation of system/data entries, size/alignment/count bounds passed.')
