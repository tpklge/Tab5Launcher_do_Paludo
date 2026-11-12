from pathlib import Path
import os,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]; here=root/'tests/installer'; sdk=Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/esp-idf'))); ssl=Path('/opt/homebrew/opt/openssl@3')
sys.path.insert(0,str(root/'tests/firmware'))
from fixtures import image
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp); sd=d/'sd'; sd.mkdir(); source=d/'large.bin'; source.write_bytes(image(payload=b'\x01\x02\x03\x04'*(5*1024*1024//4)))
 binary=d/'test'; cmd=['cc','-std=c99','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined',f'-DSD_FILES_ROOT="{sd}"',f'-DLAUNCHER_ROOT="{sd}/launcher"','-I',str(here/'stubs'),'-I',str(sdk/'components/json/cJSON')]
 for comp in ('wifi','catalog','installer','firmware','storage','boot_mgr','partitions_mgr'): cmd+=['-I',str(root/'components'/comp/'include')]
 if ssl.exists(): cmd+=['-I',str(ssl/'include'),'-L',str(ssl/'lib')]
 cmd += [str(here/'test_large.c'),str(root/'components/installer/installer.c'),str(root/'tests/installer/feature_stubs.c'),str(root/'components/firmware/firmware.c'),str(root/'components/firmware/firmware_parser.c'),str(root/'components/storage/sd_files.c'),str(root/'components/storage/storage_guard.c'),str(sdk/'components/json/cJSON/cJSON.c'),'-lcrypto','-o',str(binary)]
 subprocess.run(cmd,check=True); subprocess.run([str(binary),str(source)],check=True)
