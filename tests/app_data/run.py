from pathlib import Path
import tempfile,subprocess,os
root=Path(__file__).resolve().parents[2];env=os.environ.copy();env.setdefault('SDKROOT','/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk')
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test';cmd=['cc','-std=c99','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined',f'-DLAUNCHER_ROOT="{tmp}/launcher"']
 for folder in ['tests/app_data/stubs','tests/installer/stubs','components/storage/include']:cmd+=['-I',str(root/folder)]
 cmd+=['-I','/opt/homebrew/opt/openssl@3/include','-L','/opt/homebrew/opt/openssl@3/lib',str(root/'tests/app_data/test.c'),str(root/'components/storage/app_data.c'),str(root/'components/storage/launcher_state.c'),str(root/'components/storage/storage_guard.c'),'-lcrypto','-o',str(p)]
 subprocess.run(cmd,env=env,check=True);subprocess.run([str(p)],env=env,check=True)
