#include "installer.h"
#include "flash_snapshot.h"
bool flash_operation_pending(void){return false;}
bool app_data_recovery_required(void){return false;}
esp_err_t app_data_save_active(app_data_progress_fn c,void*u,char*m,size_t n){return 0;}
esp_err_t app_data_activate(int slot,app_data_progress_fn c,void*u,char*m,size_t n){return 0;}
esp_err_t app_data_release_owner(void){return 0;}
esp_err_t app_data_recover(bool r,app_data_progress_fn c,void*u,char*m,size_t n){return 0;}
esp_err_t app_data_configure(const app_data_config_t*a,bool b,app_data_progress_fn c,void*u,char*m,size_t n){return 0;}
esp_err_t app_data_inherit(const uint8_t*a,const uint8_t*b,app_data_progress_fn c,void*u,char*m,size_t n){return 0;}
esp_err_t catalog_track_install(const char*p,const uint8_t*i){return ESP_ERR_NOT_FOUND;}
esp_err_t flash_migrate_apply(const uint32_t*s,unsigned c,const uint8_t*h,partition_progress_fn p,void*u,char*m,size_t n){return ESP_ERR_NOT_SUPPORTED;}
esp_err_t flash_snapshot_create(flash_snapshot_t*s,partition_progress_fn p,void*u,char*m,size_t n){return ESP_ERR_NOT_SUPPORTED;}
esp_err_t flash_snapshot_restore(const flash_snapshot_t*s,partition_progress_fn p,void*u,char*m,size_t n){return ESP_ERR_NOT_SUPPORTED;}
esp_err_t flash_operation_resume(partition_progress_fn p,void*u,char*m,size_t n){return ESP_ERR_NOT_SUPPORTED;}

bool app_data_get(const uint8_t i[32],app_data_config_t*c){return false;}

esp_err_t wifi_pause_wait(uint32_t timeout){return ESP_OK;}void wifi_resume(void){}
esp_err_t flash_migrate_plan(const uint32_t*s,unsigned c,uint8_t h[32],char*m,size_t n){return ESP_ERR_NOT_SUPPORTED;}

esp_err_t app_data_capture_return(app_data_progress_fn cb,void*u,char*m,size_t n){return 0;}

esp_err_t flash_allocate_apply(uint32_t r,const uint8_t*h,partition_progress_fn cb,void*u,char*m,size_t n){return ESP_ERR_NOT_SUPPORTED;}

esp_err_t catalog_alias_download(const char*a,const char*b){return ESP_ERR_NOT_FOUND;}

esp_err_t flash_layout_apply(const uint32_t*s,const uint8_t*h,partition_progress_fn cb,void*u,char*m,size_t n){return ESP_ERR_NOT_SUPPORTED;}

esp_err_t flash_allocate_apply_options(uint32_t r,const uint8_t*h,bool b,partition_progress_fn cb,void*u,char*m,size_t n){return flash_allocate_apply(r,h,cb,u,m,n);}
esp_err_t flash_layout_apply_options(const uint32_t*s,const uint8_t*h,bool b,partition_progress_fn cb,void*u,char*m,size_t n){return flash_layout_apply(s,h,cb,u,m,n);}

esp_err_t flash_snapshot_restore_options(const flash_snapshot_t*s,bool b,partition_progress_fn cb,void*u,char*m,size_t n){return flash_snapshot_restore(s,cb,u,m,n);}
