from pathlib import Path
import subprocess,tempfile,hashlib,os,shutil
root=Path(__file__).resolve().parents[2];binary=root/'sdcard/launcher/c6/network_adapter.bin'
with tempfile.TemporaryDirectory() as tmp:
 base=Path(tmp);(base/'launcher/c6').mkdir(parents=True);shutil.copyfile(binary,base/'launcher/c6/network_adapter.bin');exe=base/'test';idf=Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/esp-idf')));ssl=Path('/opt/homebrew/opt/openssl@3')
 command=['cc','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined',f'-DLAUNCHER_ROOT="{base}/launcher"']
 for include in ('tests/c6_update/stubs','tests/installer/stubs','components/usb_mgr/include','components/usb_disk/include','components/installer/include','components/firmware/include','components/storage/include','components/wifi/include'):command+=['-I',str(root/include)]
 command+=['-I',str(idf/'components/json/cJSON'),'-I',str(ssl/'include'),'-L',str(ssl/'lib'),str(root/'tests/c6_update/test.c'),str(root/'components/firmware/firmware_parser.c'),str(idf/'components/json/cJSON/cJSON.c'),'-lcrypto','-o',str(exe)]
 subprocess.run(command,check=True);subprocess.run([str(exe),hashlib.sha256(binary.read_bytes()).hexdigest()],check=True)
