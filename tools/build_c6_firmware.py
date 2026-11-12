#!/usr/bin/env python3
"""Build the pinned ESP-Hosted SDIO coprocessor for Tab5. Never flashes a device.
Run inside the ESP-IDF 5.4.4 export environment; outputs /launcher/c6 assets.
"""
from pathlib import Path
import argparse,shutil,subprocess,json,hashlib,os
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--work',type=Path,required=True);p.add_argument('--out',type=Path,default=ROOT/'sdcard/launcher/c6');a=p.parse_args()
if a.work.exists():p.error('--work must be a new directory')
source=ROOT/'managed_components/espressif__esp_hosted';a.work.mkdir(parents=True)
shutil.copytree(source/'slave',a.work/'slave',ignore=shutil.ignore_patterns('build','sdkconfig','sdkconfig.old','managed_components','.git'));shutil.copytree(source/'common',a.work/'common')
project=a.work/'slave'
with (project/'sdkconfig.defaults').open('a') as f:f.write('\nCONFIG_ESP_SDIO_HOST_INTERFACE=y\nCONFIG_ESP_SPI_HOST_INTERFACE=n\nCONFIG_ESP_HOSTED_NETWORK_SPLIT_ENABLED=n\nCONFIG_COMPILER_OPTIMIZATION_SIZE=y\n')
subprocess.run(['idf.py','-C',str(project),'set-target','esp32c6'],check=True)
subprocess.run(['idf.py','-C',str(project),'build'],check=True)
config=(project/'sdkconfig').read_text()
for value in ['CONFIG_IDF_TARGET="esp32c6"','CONFIG_ESP_SDIO_HOST_INTERFACE=y']:
 if value not in config:raise RuntimeError('Unexpected coprocessor config: '+value)
image=project/'build/network_adapter.bin';data=image.read_bytes()
if data[0]!=0xe9 or int.from_bytes(data[12:14],'little')!=0x0d or b'2.13.1' not in data[:300]:raise RuntimeError('Not expected C6 / 2.13.1 image')
a.out.mkdir(parents=True,exist_ok=True);shutil.copyfile(image,a.out/image.name);shutil.copyfile(project/'sdkconfig',a.out/'sdkconfig.build')
manifest=dict(board='M5Stack Tab5',transport='SDIO',version='2.13.1',idf='5.4.4',sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),configSha256=hashlib.sha256(config.encode()).hexdigest(),source='espressif/esp_hosted==2.13.1')
(a.out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest))
