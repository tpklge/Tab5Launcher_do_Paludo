from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'web_test'
 subprocess.run(['cc','-g','-fsanitize=address,undefined','-I',str(root/'components/web/include'),str(root/'components/web/web_security.c'),str(root/'tests/web/test.c'),'-o',str(p)],check=True)
 subprocess.run([str(p)],check=True)
