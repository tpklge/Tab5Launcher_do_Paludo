from pathlib import Path
import tempfile,subprocess,os
root=Path(__file__).resolve().parents[2];idf=Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/esp-idf')))
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test';cmd=['cc','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined',f'-DLAUNCHER_ROOT="{tmp}/launcher"']
 for folder in ('tests/catalog_http/stubs','tests/usb/stubs','tests/installer/stubs','components/catalog/include','components/storage/include'):cmd+=['-I',str(root/folder)]
 cmd+=['-I',str(idf/'components/json/cJSON')]
 ssl=Path('/opt/homebrew/opt/openssl@3')
 if ssl.exists():cmd+=['-I',str(ssl/'include'),'-L',str(ssl/'lib')]
 cmd+=[str(root/'tests/catalog_http/test.c'),str(root/'components/catalog/catalog.c'),str(root/'components/catalog/catalog_cache.c'),str(root/'components/catalog/catalog_model.c'),str(root/'components/catalog/catalog_adapters.c'),str(idf/'components/json/cJSON/cJSON.c'),'-lcrypto','-o',str(p)]
 cmd.append(str(root/'components/storage/storage_guard.c'))
 subprocess.run(cmd,check=True);subprocess.run([str(p)],check=True)
