"""Compile the production storage component with host filesystem/RTOS doubles."""
from pathlib import Path
import subprocess
import tempfile
import os

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    d = Path(tmp)
    headers = {
        'esp_err.h': '#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NOT_FOUND 1\n#define ESP_ERR_INVALID_ARG 2\n#define ESP_ERR_NO_MEM 3\n#define ESP_ERR_INVALID_STATE 4\n#define ESP_ERR_INVALID_CRC 5\n#define ESP_ERR_INVALID_SIZE 6\n',
        'esp_log.h': '#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n',
        'esp_check.h': '#define ESP_RETURN_ON_ERROR(expr,...) do { int rc=(expr); if(rc) return rc; } while(0)\n',
        'nvs.h': '''#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
static int fail_write;
static int nvs_open(const char *ns, int mode, nvs_handle_t *h) { *h=1; return mode==NVS_READONLY ? 1:0; }
static void nvs_close(nvs_handle_t h) {}
static int nvs_commit(nvs_handle_t h) { return 0; }
static int nvs_get_u8(nvs_handle_t h,const char*k,uint8_t*v) { return 1; }
static int nvs_get_u16(nvs_handle_t h,const char*k,uint16_t*v) { return 1; }
static int nvs_get_str(nvs_handle_t h,const char*k,char*v,size_t*n) { return 1; }
static int nvs_set_u8(nvs_handle_t h,const char*k,uint8_t v) { return fail_write; }
static int nvs_set_u16(nvs_handle_t h,const char*k,uint16_t v) { return fail_write; }
static int nvs_set_str(nvs_handle_t h,const char*k,const char*v) { return fail_write; }
''',
        'nvs_flash.h': '',
        'freertos/FreeRTOS.h': '#pragma once\n#define portMAX_DELAY 0\n',
        'freertos/semphr.h': '''typedef void *SemaphoreHandle_t;
static void *xSemaphoreCreateMutex(void) { return (void*)1; }
#define xSemaphoreTake(...) ((void)0)
#define xSemaphoreGive(...) ((void)0)
''',
    }
    for name, text in headers.items():
        p = d / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
    source = d / 'test.c'
    source.write_text(r'''#include <assert.h>
#include "STORAGE_SOURCE"
static int fail_write;
int main(int argc, char **argv) {
    storage_init();
    assert(storage_import_json(NULL) == ESP_ERR_INVALID_ARG);
    assert(storage_import_json("/missing-tab5-config") == ESP_ERR_NOT_FOUND);
    assert(strcmp(storage_get_config()->theme, "graphite") == 0);
    assert(storage_import_json(argv[1]) == ESP_OK);
    assert(strcmp(storage_get_config()->theme, "neon") == 0);
    assert(storage_get_config()->bright == 42);
    const char *new_themes[] = {"ocean","forest","sunset","lavender","sand"};
    launcher_config_t themes_before=s_cfg;
    for(unsigned i=0;i<5;i++) {
      char text[80];snprintf(text,sizeof(text),"{\"theme\":\"%s\"}",new_themes[i]);
      assert(config_json_decode(text,&s_cfg)==ESP_OK);
      assert(!strcmp(s_cfg.theme,new_themes[i]));
    }
    assert(config_json_decode("{\"theme\":\"unknown\"}",&s_cfg)==ESP_ERR_INVALID_ARG);
    s_cfg=themes_before;
    launcher_config_t before=*storage_get_config();
    assert(config_json_decode("{\"bright\":999}", &s_cfg) == ESP_ERR_INVALID_ARG);
    assert(memcmp(&before,&s_cfg,sizeof(before))==0);
    assert(config_json_decode("{\"bright\":42,\"bright\":43}", &s_cfg) == ESP_ERR_INVALID_ARG);
    assert(config_json_decode("{\"schema\":99}", &s_cfg) == ESP_ERR_INVALID_ARG);
    assert(config_json_decode("{\"timezone\":true}", &s_cfg) == ESP_ERR_INVALID_ARG);
    char *encoded=config_json_encode(&s_cfg); assert(encoded);
    launcher_config_t roundtrip=s_cfg;
    assert(config_json_decode(encoded,&roundtrip)==ESP_OK);
    assert(memcmp(&roundtrip,&s_cfg,sizeof(s_cfg))==0); free(encoded);
    char deep[256];memset(deep,'[',100);memset(deep+100,']',100);deep[200]=0;
    assert(config_json_decode(deep,&s_cfg)==ESP_ERR_INVALID_ARG);
    fail_write = 77;
    assert(storage_save() == ESP_OK);
    before=s_cfg;launcher_config_t change=s_cfg;change.bright=65;
    assert(storage_update(&change)==ESP_OK && s_cfg.bright==65);
    fail_write=0;
    assert(storage_import_theme(argv[2])==ESP_OK && !strcmp(s_cfg.theme,"midnight"));
    before=s_cfg;
    assert(storage_import_theme(argv[3])==ESP_ERR_INVALID_ARG && memcmp(&before,&s_cfg,sizeof(before))==0);
    /* Simulate boot with only a retained configuration, then save again. */
    before=s_cfg;assert(!rename(STORAGE_CFG_PATH,STORAGE_CFG_PATH ".previous"));storage_init();assert(!memcmp(&before,&s_cfg,sizeof(before)));change=s_cfg;change.bright=72;assert(!storage_update(&change));
    /* Valid old/new files after an interrupted final cleanup. */
    FILE *old=fopen(STORAGE_CFG_PATH ".previous","wb");assert(old);fputs("{\"schema\":1,\"bright\":13}",old);fclose(old);storage_init();assert(s_cfg.bright==72);assert(!storage_save());
    /* Corrupt primary must recover the validated previous without losing it. */
    assert(!rename(STORAGE_CFG_PATH,STORAGE_CFG_PATH ".previous"));FILE *bad=fopen(STORAGE_CFG_PATH,"wb");assert(bad);fputs("{",bad);fclose(bad);storage_init();assert(s_cfg.bright==72);assert(!storage_save());
    /* First publication: synced, valid tmp, no principal yet. */
    assert(!rename(STORAGE_CFG_PATH,STORAGE_CFG_PATH ".tmp"));storage_init();assert(s_cfg.bright==72);assert(!storage_save());
    return 0;
}
'''.replace('STORAGE_SOURCE',str(ROOT/'components/storage/storage.c')))
    config = d / 'custom.json'
    config.write_text('{"schema":1,"theme":"neon","bright":42}')
    theme=d/'theme.json';theme.write_text('{"schema":1,"theme":"midnight","color":"AA3344"}')
    badtheme=d/'bad-theme.json';badtheme.write_text('{"schema":1,"wifiAuto":true}')
    binary = d / 'test'
    subprocess.run(['cc', '-fsanitize=address,undefined', '-g', '-I', str(d), '-I',
                    str(ROOT / 'components/storage/include'), str(source), str(ROOT/'components/storage/launcher_store.c'), str(ROOT/'components/storage/sd_files.c'),str(ROOT/'components/storage/storage_guard.c'), str(ROOT/'components/storage/launcher_paths.c'), '-I', str(ROOT/'tests/installer/stubs'), '-I', '/opt/homebrew/opt/openssl@3/include', '-L', '/opt/homebrew/opt/openssl@3/lib', '-lcrypto', '-Wno-deprecated-declarations', '-DLAUNCHER_ROOT=\"'+str(d/'launcher')+'\"', '-DSD_FILES_ROOT=\"'+str(d)+'\"', str(ROOT/'components/storage/config_json.c'), str(Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/esp-idf')))/'components/json/cJSON/cJSON.c'), '-I', str(Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/esp-idf')))/'components/json/cJSON'), '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(config), str(theme), str(badtheme)], check=True)
print('Storage host regressions passed (ASan/UBSan).')
