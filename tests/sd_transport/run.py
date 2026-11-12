from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[2]
env=os.environ.copy();env.setdefault('SDKROOT','/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk')
with tempfile.TemporaryDirectory() as tmp:
 binary=Path(tmp)/'test';subprocess.run(['cc','-std=c99','-g','-fsanitize=address,undefined','-I',str(root/'tests/sd_transport/stubs'),str(root/'tests/sd_transport/test.c'),'-o',str(binary)],check=True,env=env);subprocess.run([str(binary)],check=True,env=env)
