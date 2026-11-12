from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]; here=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    output=str(Path(tmp)/'boot_test')
    subprocess.run(['cc','-g','-fsanitize=address,undefined','-I',str(here/'stubs'),'-I',str(root/'components/boot_mgr/include'),
                    str(here/'test.c'),str(root/'bootloader_components/launcher_policy/hooks.c'),'-o',output],check=True)
    subprocess.run([output],check=True)
