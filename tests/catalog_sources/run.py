from pathlib import Path
import os,subprocess,tempfile
r=Path(__file__).resolve().parents[2];idf=Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/esp-idf')))
with tempfile.TemporaryDirectory() as t:
 d=Path(t);cmd=['cc','-g','-fsanitize=address,undefined',f'-DLAUNCHER_ROOT="{d}"']
 for inc in ('tests/installer/stubs','components/catalog/include','components/storage/include'):cmd+=['-I',str(r/inc)]
 cmd+=['-I',str(idf/'components/json/cJSON'),str(r/'tests/catalog_sources/test.c'),str(r/'components/catalog/catalog_sources.c'),str(idf/'components/json/cJSON/cJSON.c'),'-o',str(d/'test')]
 cmd.append(str(r/'components/storage/storage_guard.c'))
 subprocess.run(cmd,check=True);subprocess.run([str(d/'test')],check=True)
