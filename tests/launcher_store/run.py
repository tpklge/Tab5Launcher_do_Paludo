from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp);(d/'freertos').mkdir()
 (d/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_INVALID_STATE 2\n#define ESP_ERR_NO_MEM 3\n#define ESP_ERR_NOT_FOUND 4\n#define ESP_ERR_INVALID_SIZE 5\n#define ESP_ERR_INVALID_CRC 6\n')
 (d/'freertos/FreeRTOS.h').write_text('#pragma once\n#define portMAX_DELAY 0\n')
 (d/'freertos/semphr.h').write_text('#pragma once\ntypedef void *SemaphoreHandle_t;\nstatic void*xSemaphoreCreateMutex(void){return (void*)1;}\nstatic int xSemaphoreTake(void*p,int t){return 1;}\nstatic int xSemaphoreGive(void*p){return 1;}\n')
 cmd=['cc','-g','-fsanitize=address,undefined',f'-DLAUNCHER_ROOT="{d}/launcher"',f'-DSD_FILES_ROOT="{d}"','-I',str(d),'-I',str(root/'components/storage/include'),str(root/'components/storage/launcher_store.c'),str(root/'components/storage/sd_files.c'),str(root/'components/storage/storage_guard.c'),str(root/'tests/launcher_store/test.c'),'-o',str(d/'test')]
 subprocess.run(cmd,check=True);subprocess.run([str(d/'test')],check=True)
