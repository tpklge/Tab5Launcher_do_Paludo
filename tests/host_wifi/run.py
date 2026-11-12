from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    binary = str(Path(tmp) / 'test_wifi')
    subprocess.run(['cc', '-std=c99', '-g', '-fsanitize=address,undefined',
                    '-I',str(root/'components/storage/include'),'-I', str(here / 'stubs'), '-I', str(root / 'components/wifi/include'),
                    str(here / 'test_wifi.c'), str(root / 'components/wifi/wifi_store.c'),
                    str(root / 'components/wifi/wifi_link.c'), '-o', binary], check=True)
    subprocess.run([binary], check=True)
