from pathlib import Path
import os,tempfile,subprocess
root=Path(__file__).resolve().parents[2];env=os.environ.copy();env.setdefault('SDKROOT','/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk')
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test';cmd=['cc','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined']
 for folder in ['tests/catalog_http/stubs','tests/usb/stubs','tests/installer/stubs','components/catalog/include','components/storage/include']:cmd+=['-I',str(root/folder)]
 cmd+=['-I','/opt/homebrew/opt/openssl@3/include','-L','/opt/homebrew/opt/openssl@3/lib',str(root/'tests/catalog_resume/test.c'),str(root/'components/catalog/catalog_transfer.c'),'-lcrypto','-o',str(p)]
 cmd.append(str(root/'components/storage/storage_guard.c'))
 subprocess.run(cmd,check=True,env=env);subprocess.run([str(p),tmp],check=True,env=env)
