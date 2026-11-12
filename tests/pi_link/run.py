from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'pi_test'
 subprocess.run(['cc','-std=c11','-g','-fsanitize=address,undefined','-I',str(root/'tests/pi_link/stubs'),'-I',str(root/'components/pi_link/include'),str(root/'tests/pi_link/test.c'),'-o',str(p)],check=True)
 subprocess.run([str(p)],check=True)
