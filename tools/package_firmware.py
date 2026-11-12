"""Package build-generated offsets, hashes and first-install/update firmware.
Run with the activated IDF Python environment after idf.py build.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import struct
p=argparse.ArgumentParser(); p.add_argument('--build',default='build'); p.add_argument('--output',default='dist')
a=p.parse_args(); build=Path(a.build).resolve(); out=Path(a.output).resolve(); out.mkdir(parents=True,exist_ok=True)
if 'TAB5_UI_NAV_SELFTEST:BOOL=ON' in (build/'CMakeCache.txt').read_text(): raise SystemExit('Disable TAB5_UI_NAV_SELFTEST before packaging production firmware')
subprocess.run([sys.executable,str(Path(__file__).resolve().parent/'check_ui_stack.py'),'--build',str(build)],check=True)
args=json.loads((build/'flasher_args.json').read_text()); items=[]
for offset,name in args['flash_files'].items():
    source=build/name; target=out/source.name; shutil.copy2(source,target)
    data=target.read_bytes(); items.append({'offset':int(offset,0),'file':target.name,'size':len(data),'sha256':hashlib.sha256(data).hexdigest()})
# The build reservation permits linking; distributed layouts reserve only the
# actual image rounded to the SDK's 64 KiB application alignment.
from prepare_dynamic_upgrade import prepare
seed=bytearray(b'\xff'*0x1000000)
seed[0x8000:0x8000+len((build/args['partition-table']['file']).read_bytes())]=(build/args['partition-table']['file']).read_bytes()
table,_,minimal=prepare(bytes(seed),(build/args['app']['file']).read_bytes())
(out/Path(args['partition-table']['file']).name).write_bytes(table[:3072])
for item in items:
    if item['offset']==0x8000:
        data=(out/item['file']).read_bytes();item.update(size=len(data),sha256=hashlib.sha256(data).hexdigest())
request_partition=None
for offset in range(0,len(table)-31,32):
    magic,kind,subtype,address,size,label,flags=struct.unpack_from('<HBBII16sI',table,offset)
    if magic!=0x50aa: break
    if label.split(b'\0')[0]==b'launch_req': request_partition=(address,size)
if request_partition is None: raise SystemExit('Missing launch_req partition in generated table')
address,size=request_partition
request=out/'launch-request-empty.bin'; request.write_bytes(b'\xff'*size)
items.append({'offset':address,'file':request.name,'size':size,'sha256':hashlib.sha256(request.read_bytes()).hexdigest()})
items.sort(key=lambda x:x['offset'])
for left,right in zip(items,items[1:]):
    if left['offset']+left['size']>right['offset']: raise SystemExit('Overlapping firmware artifacts')
manifest={'chip':args['extra_esptool_args']['chip'],'flash_settings':args['flash_settings'],'initial_install':items,
          'update':{'offset':int(args['app']['offset'],0),'file':Path(args['app']['file']).name},
          'feature_update':[item for item in items if item['offset'] in (int(args['bootloader']['offset'],0),int(args['app']['offset'],0))],
          'minimum_launcher_size':minimal['launcher_size'],
          'note':'For dynamic upgrades use tools/prepare_dynamic_upgrade.py with a fresh full backup. Never flash the build reservation table over an existing device. Initial merged install clears configuration in gaps. Updating only the app preserves layout; the 8 MB to two 4 MB slot upgrade requires a full backup and tools/prepare_multiboot_upgrade.py before writing the separate partition table; one-shot bootloader must already be installed. The 0.5.0 profile editor migrates apps within the fixed 8 MiB app bank using a verified full snapshot, mirrored table and operation guard at 0x15000; it requires the mirrored-table recovery bootloader. Self-hosted full restore requires identical bootloader/launcher; cross-version full restore uses the USB ROM utility. feature_update writes bootloader+launcher while preserving the current table/data.'}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
command=[sys.executable,'-m','esptool','--chip',manifest['chip'],'merge_bin','-o',str(out/'tab5-launcher-initial.bin')]
command+=args['write_flash_args']
for item in items: command += [hex(item['offset']),str(out/item['file'])]
subprocess.run(command,check=True)
# Web assets are delivered separately, stored and served from the microSD.
web_source=Path(__file__).resolve().parents[1]/'sdcard/launcher/web'
web_dest=out/'sdcard/launcher/web'
shutil.copytree(web_source,web_dest,dirs_exist_ok=True)
assets=[{'file':f.name,'size':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in sorted(web_dest.iterdir()) if f.is_file()]
(out/'sdcard-assets.json').write_text(json.dumps({'target':'/launcher/web','files':assets,'preserve':'Existing /launcher/config.json and /launcher/private are never shipped or overwritten'},indent=2)+'\n')
shutil.copy2(web_source.parent/'ota_sources.json', web_dest.parent/'ota_sources.example.json')
shutil.make_archive(str(out/'tab5-microsd-web'),'zip',root_dir=str(out/'sdcard'))
files=[x for x in out.iterdir() if x.is_file() and x.name!='SHA256SUMS']
(out/'SHA256SUMS').write_text(''.join(hashlib.sha256(x.read_bytes()).hexdigest()+'  '+x.name+'\n' for x in sorted(files)))
print(out/'manifest.json')
