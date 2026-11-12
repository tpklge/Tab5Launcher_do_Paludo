from pathlib import Path
import tempfile,subprocess,os
root=Path(__file__).resolve().parents[2]; ssl=Path('/opt/homebrew/opt/openssl@3')
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp); sd=d/'sd'; sd.mkdir(); (sd/'launcher').mkdir(); binary=d/'test'
 cmd=['cc','-std=c99','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined',f'-DSD_FILES_ROOT="{sd}"',f'-DLAUNCHER_ROOT="{sd}/launcher"','-I',str(root/'tests/installer/stubs')]
 for comp in ('partitions_mgr','boot_mgr','storage'): cmd+=['-I',str(root/'components'/comp/'include')]
 if ssl.exists(): cmd+=['-I',str(ssl/'include'),'-L',str(ssl/'lib')]
 cmd+=[str(root/'tests/layout/test_transaction.c'),str(root/'components/partitions_mgr/partitions_mgr.c'),str(root/'components/partitions_mgr/layout_model.c'),'-lcrypto','-o',str(binary)]
 subprocess.run(cmd,check=True); subprocess.run([str(binary),str(root/'tests/fixtures/legacy-partition-table.bin')],check=True)
