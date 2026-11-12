from pathlib import Path
import os,tempfile,subprocess
root=Path(__file__).resolve().parents[2];env=os.environ.copy();env.setdefault('SDKROOT','/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk')
with tempfile.TemporaryDirectory() as tmp:
 sd=Path(tmp)/'sd';sd.mkdir();p=Path(tmp)/'test';cmd=['cc','-std=c99','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined',f'-DSD_FILES_ROOT="{sd}"',f'-DLAUNCHER_ROOT="{sd}/launcher"','-I',str(root/'tests/installer/stubs')]
 for c in ['partitions_mgr','boot_mgr','storage']:cmd+=['-I',str(root/'components'/c/'include')]
 cmd+=['-I','/opt/homebrew/opt/openssl@3/include','-L','/opt/homebrew/opt/openssl@3/lib',str(root/'tests/flash_snapshot/test.c'),str(root/'components/partitions_mgr/flash_snapshot.c'),str(root/'components/partitions_mgr/partitions_mgr.c'),str(root/'components/partitions_mgr/layout_model.c'),str(root/'components/storage/sd_files.c'),str(root/'components/storage/storage_guard.c'),'-lcrypto','-o',str(p)]
 subprocess.run(cmd,env=env,check=True);subprocess.run([str(p),str(root/'tests/fixtures/legacy-partition-table.bin')],env=env,check=True)
