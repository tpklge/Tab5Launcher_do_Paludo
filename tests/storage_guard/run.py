from pathlib import Path
import subprocess,tempfile,os
r=Path(__file__).resolve().parents[2]
env=os.environ.copy();sdk='/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk'
if Path(sdk).exists():env.setdefault('SDKROOT',sdk)
with tempfile.TemporaryDirectory() as t:
 p=Path(t)/'test'
 subprocess.run(['cc','-std=gnu11','-g','-pthread','-fsanitize=address,undefined','-I',str(r/'components/storage/include'),str(r/'tests/storage_guard/test.c'),str(r/'components/storage/storage_guard.c'),'-o',str(p)],check=True,env=env)
 subprocess.run([str(p)],check=True,env=env)
