from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
 exe=Path(tmp)/'test';ssl=Path('/opt/homebrew/opt/openssl@3')
 command=['cc','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined',f'-DSD_FILES_ROOT="{tmp}/sd"',f'-DLAUNCHER_ROOT="{tmp}/sd/launcher"']
 for include in ('tests/installer/stubs','components/pi_link/include','components/storage/include'):command+=['-I',str(root/include)]
 command+=['-I',str(ssl/'include'),'-L',str(ssl/'lib'),str(root/'tests/pi_services/test.c'),str(root/'components/pi_link/pi_services.c'),str(root/'components/storage/sd_files.c'),str(root/'components/storage/storage_guard.c'),'-lcrypto','-o',str(exe)]
 subprocess.run(command,check=True);subprocess.run([str(exe)],check=True)
