from pathlib import Path
import tempfile,subprocess,os
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp);command=['cc','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined',f'-DUSB_INCOMING_ROOT="{d}/incoming"']
 for include in ('tests/usb/stubs','tests/pi_link/stubs','tests/installer/stubs','components/catalog/include','components/usb_mgr/include','components/usb_disk/include','components/diagnostics/include','components/boot_mgr/include','components/backup/include','components/installer/include','components/firmware/include','components/storage/include','components/wifi/include'):
  command+=['-I',str(root/include)]
 idf=Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/esp-idf')));command+=['-I',str(idf/'components/json/cJSON')]
 openssl=Path('/opt/homebrew/opt/openssl@3')
 if openssl.exists():command+=['-I',str(openssl/'include'),'-L',str(openssl/'lib')]
 command += [str(root/'tests/usb/test.c'),str(root/'components/storage/sd_files.c'),str(root/'components/storage/storage_guard.c'),str(idf/'components/json/cJSON/cJSON.c'),'-lcrypto','-o',str(d/'test')]
 subprocess.run(command,check=True);subprocess.run([str(d/'test')],check=True)
