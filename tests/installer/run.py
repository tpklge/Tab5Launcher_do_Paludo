from pathlib import Path
import subprocess
import sys
import os
import json,hashlib
import tempfile
root=Path(__file__).resolve().parents[2]; here=Path(__file__).resolve().parent
sys.path.insert(0,str(root/'tests/firmware'))
from fixtures import image
openssl=Path('/opt/homebrew/opt/openssl@3')
sdk=Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/esp-idf')))
with tempfile.TemporaryDirectory() as tmp:
    folder=Path(tmp); sd=folder/'sd'; sd.mkdir()
    first=folder/'first.bin'; first.write_bytes(image())
    second=folder/'second.bin'; second.write_bytes(image(version=b'2.0',payload=b'\x05\x06\x07\x08'*2048))
    (sd/'app.bin').write_bytes(image()); (sd/'data.bin').write_bytes(bytes(range(256))*32)
    (sd/'package.json').write_text(json.dumps({'schema':1,'chip':'esp32p4','app':{'file':'app.bin','sha256':hashlib.sha256((sd/'app.bin').read_bytes()).hexdigest()},'data':[{'file':'data.bin','label':'vfs','sha256':hashlib.sha256((sd/'data.bin').read_bytes()).hexdigest()}]}))
    binary=folder/'installer_test'
    cmd=['cc','-g','-std=c99','-Wno-deprecated-declarations','-fsanitize=address,undefined',f'-DSD_FILES_ROOT="{sd}"',f'-DLAUNCHER_ROOT="{sd}/launcher"',
         '-I',str(here/'stubs')]
    for comp in ('wifi','catalog','installer','firmware','storage','boot_mgr','partitions_mgr'): cmd += ['-I',str(root/'components'/comp/'include')]
    if openssl.exists(): cmd += ['-I',str(openssl/'include'),'-L',str(openssl/'lib')]
    cmd += ['-I',str(sdk/'components/json/cJSON'),str(sdk/'components/json/cJSON/cJSON.c')]
    cmd += [str(here/'test.c'),str(root/'components/installer/installer.c'),str(root/'tests/installer/feature_stubs.c'),str(root/'components/firmware/firmware.c'),
            str(root/'components/firmware/firmware_parser.c'),str(root/'components/storage/sd_files.c'),str(root/'components/storage/storage_guard.c'),'-lcrypto','-o',str(binary)]
    subprocess.run(cmd,check=True); subprocess.run([str(binary),str(first),str(second)],check=True)
