from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
idf=Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/esp-idf')))
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp)
 (d/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n')
 cmd=['cc','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I',str(d),'-I',str(root/'components/catalog/include'),'-I',str(root/'components/storage/include'),'-I',str(idf/'components/json/cJSON'),str(root/'components/catalog/catalog_model.c'),str(root/'tests/catalog/test.c'),str(idf/'components/json/cJSON/cJSON.c'),'-o',str(d/'test')]
 subprocess.run(cmd,check=True);subprocess.run([str(d/'test')],check=True)

with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp);(d/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n')
 command=['cc','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I',str(d),'-I',str(root/'components/catalog/include'),'-I',str(root/'components/storage/include'),'-I',str(idf/'components/json/cJSON'),str(root/'components/catalog/catalog_model.c'),str(root/'components/catalog/catalog_adapters.c'),str(root/'tests/catalog/test_adapters.c'),str(idf/'components/json/cJSON/cJSON.c'),'-o',str(d/'test')]
 subprocess.run(command,check=True)
 subprocess.run([str(d/'test')],check=True)
 for fixture,url in [('/tmp/tab5-feed.json','https://api.launcherhub.net/giveMeTheList'),('/tmp/tab5-m5official-feed.json','https://m5burner-api.m5stack.com/api/firmware')]:
  if Path(fixture).exists():subprocess.run([str(d/'test'),fixture,url],check=True)
