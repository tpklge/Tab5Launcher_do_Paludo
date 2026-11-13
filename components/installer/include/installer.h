#pragma once
#include "firmware.h"
#include "sd_files.h"
typedef struct {
  char path[SD_PATH_MAX], target[17], replaced_project[33];
  uint32_t address, size;
  int slot;
  bool replaces_app;
  bool backup_before_changes;
  uint8_t target_sha256[32];
  firmware_info_t firmware;
  bool needs_layout;
  uint8_t layout_sha256[32], displaced_sha256[32], data_sha256[FW_DATA_MAX][32];
} install_plan_t;
typedef bool (*installer_progress_fn)(unsigned percent, const char *stage,
                                      void *user);
esp_err_t installer_init(void);
esp_err_t installer_plan(const char *path, install_plan_t *plan, char *error,
                         size_t error_size);
/* App only; caller must explicitly accept unknown data requirements. Never
 * selects boot. */
esp_err_t installer_execute(const install_plan_t *plan,
                            installer_progress_fn progress, void *user,
                            char *message, size_t size);
/* Full-slot digest binds a destructive confirmation to the exact current bytes.
 */
esp_err_t installer_slot_digest(int slot, uint8_t digest[32]);
esp_err_t installer_uninstall(int slot, const uint8_t expected[32],
                              installer_progress_fn progress, void *user,
                              char *message, size_t size);

bool installer_is_busy(void);
/* Durable source retained over a layout reboot; UI reopens its validated plan.
 */
bool installer_pending_source(char *path, size_t size);

esp_err_t installer_cancel_pending(const char *source);
bool installer_recovery_required(void);
/* Manual verified full-slot backup, serialized with install/uninstall. */
esp_err_t installer_backup_slot(int slot,installer_progress_fn progress,void *user,char *message,size_t size);

#define INSTALLER_DATA_OPTIONS 8
typedef struct {char label[17];uint32_t size;bool associated;} installer_data_option_t;
unsigned installer_data_options(int slot,installer_data_option_t options[INSTALLER_DATA_OPTIONS]);
esp_err_t installer_uninstall_data(int slot,const uint8_t expected[32],uint32_t data_mask,installer_progress_fn,void*,char*,size_t);

esp_err_t installer_repartition(const uint32_t*sizes,unsigned count,const uint8_t expected[32],installer_progress_fn,void*,char*,size_t);

#include "app_data.h"
typedef struct flash_snapshot flash_snapshot_t;
esp_err_t installer_profile_configure(const app_data_config_t*,bool,installer_progress_fn,void*,char*,size_t);
esp_err_t installer_prepare_app_data(int,installer_progress_fn,void*,char*,size_t);
esp_err_t installer_full_backup(flash_snapshot_t*,installer_progress_fn,void*,char*,size_t);
esp_err_t installer_full_restore(const flash_snapshot_t*,installer_progress_fn,void*,char*,size_t);
esp_err_t installer_resume_flash(installer_progress_fn,void*,char*,size_t);

esp_err_t installer_recover_profile(bool,installer_progress_fn,void*,char*,size_t);

esp_err_t installer_capture_profile_return(installer_progress_fn,void*,char*,size_t);

/* Exclusive maintenance operation: blocks install/boot while callback executes. */
esp_err_t installer_maintenance(esp_err_t (*fn)(void *),void *);

esp_err_t installer_uninstall_options(int,const uint8_t[32],bool backup_binary,uint32_t,installer_progress_fn,void*,char*,size_t);

esp_err_t installer_compact(const uint8_t[32],installer_progress_fn,void*,char*,size_t);

esp_err_t installer_layout_apply(const uint32_t sizes[16],const uint8_t digest[32],installer_progress_fn,void*,char*,size_t);

esp_err_t installer_compact_options(const uint8_t[32],bool,installer_progress_fn,void*,char*,size_t);
esp_err_t installer_layout_apply_options(const uint32_t[16],const uint8_t[32],bool,installer_progress_fn,void*,char*,size_t);

esp_err_t installer_full_restore_options(const flash_snapshot_t*,bool,installer_progress_fn,void*,char*,size_t);
