from pathlib import Path
import os,sys,tempfile,hashlib,json,struct,subprocess,copy
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests/firmware'))
from fixtures import image
sdk=Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/esp-idf'))); ssl=Path('/opt/homebrew/opt/openssl@3')
def table(rows):
 b=b''.join(struct.pack('<HBBII16sI',0x50aa,*r) for r in rows)
 return b+b'\xeb\xeb'+b'\xff'*14+hashlib.md5(b).digest()
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp); sd=d/'sd'; sd.mkdir(); app=image(); data=b'\x83'*8192
 (sd/'app.bin').write_bytes(app); (sd/'data.bin').write_bytes(data)
 package={'schema':1,'chip':'esp32p4','app':{'file':'app.bin','sha256':hashlib.sha256(app).hexdigest()},'data':[{'file':'data.bin','sha256':hashlib.sha256(data).hexdigest(),'label':'spiffs'}]}
 (sd/'package.json').write_text(json.dumps(package))
 variants={}
 for name,fn in [('wrongchip',lambda j:j.update(chip='esp32s3')),('traversal',lambda j:j['app'].update(file='../app.bin')),('wronghash',lambda j:j['app'].update(sha256='00'*32)),('protected',lambda j:j['data'][0].update(label='nvs')),('unknown',lambda j:j.update(offset=123)),('bootloader',lambda j:j.update(bootloader={'file':'app.bin'}))]:
  j=copy.deepcopy(package); fn(j); (sd/(name+'.json')).write_text(json.dumps(j))
 (sd/'duplicate.json').write_text(json.dumps(package).replace('"schema": 1','"schema": 1, "schema": 2'))
 rows=[(1,2,0x9000,0x6000,b'nvs',0),(0,0,0x10000,0x100000,b'factory',0),(1,0x82,0x110000,8192,b'spiffs',0)]
 merged=bytearray(b'\xff'*0x112000); t=table(rows); merged[0x8000:0x8000+len(t)]=t; merged[0x10000:0x10000+len(app)]=app; merged[0x110000:]=data
 (sd/'merged.bin').write_bytes(merged); (sd/'partial.bin').write_bytes(merged[:-4096])
 bad=merged.copy(); bad[0x8010]^=1; (sd/'badtable.bin').write_bytes(bad)
 ambiguous=merged+bytearray(b'\xff'*0x10000); rows.append((0,16,0x120000,0x10000,b'app0',0)); t=table(rows); ambiguous[0x8000:0x8000+len(t)]=t
 ambiguous.extend(b'\xff'*(0x130000-len(ambiguous))); ambiguous[0x120000:0x120000+len(app)]=app; (sd/'ambiguous.bin').write_bytes(ambiguous)
 for name,blob,offset in [('backup',app+b'\xff'*(131072-len(app)),0x210000),('data',data,0xf16000),('broken',app,0x9000)]:
  (sd/(name+'.raw')).write_bytes(blob); (sd/(name+'.raw.json')).write_text(json.dumps({'schema':1,'kind':'partition-backup','offset':offset,'size':len(blob),'sha256':hashlib.sha256(blob).hexdigest()}))
 cmd=['cc','-std=c99','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined',f'-DSD_FILES_ROOT="{sd}"','-I',str(root/'tests/installer/stubs'),'-I',str(ssl/'include'),'-L',str(ssl/'lib'),'-I',str(sdk/'components/json/cJSON')]
 for comp in ('firmware','storage'): cmd+=['-I',str(root/'components'/comp/'include')]
 cmd += [str(root/'tests/bundles/test.c'),str(root/'components/firmware/firmware.c'),str(root/'components/firmware/firmware_parser.c'),str(root/'components/storage/sd_files.c'),str(root/'components/storage/storage_guard.c'),str(sdk/'components/json/cJSON/cJSON.c'),'-lcrypto','-o',str(d/'test')]
 subprocess.run(cmd,check=True); subprocess.run([str(d/'test')],check=True)
