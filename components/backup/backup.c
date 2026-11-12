#include "backup.h"
esp_err_t backup_init(void) {return installer_init();}
esp_err_t backup_slot(int slot,installer_progress_fn progress,void *user,char *message,size_t size) {
 return installer_backup_slot(slot,progress,user,message,size);
}
