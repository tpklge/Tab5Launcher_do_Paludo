#include "launcher_state.h"
#include "launcher_store.h"
#include "storage_guard.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void *writer(void *arg) {
  launcher_state_handle_t h;
  assert(launcher_state_open("boot_mgr",LAUNCHER_STATE_READWRITE,&h)==ESP_ERR_INVALID_STATE);
  return NULL;
}
int main(void) {
  assert(!launcher_store_init());
  launcher_state_handle_t h=NULL;
  assert(launcher_state_open("absent",LAUNCHER_STATE_READONLY,&h)==ESP_ERR_NOT_FOUND && !h);
  assert(launcher_state_open("../escape",LAUNCHER_STATE_READWRITE,&h)==ESP_ERR_INVALID_ARG);
  assert(!storage_io_busy());
  assert(!launcher_state_open("boot_mgr",LAUNCHER_STATE_READWRITE,&h));
  assert(storage_io_busy());
  pthread_t thread;assert(!pthread_create(&thread,NULL,writer,NULL));pthread_join(thread,NULL);
  assert(!launcher_state_set_i8(h,"pending_slot",3));
  assert(!launcher_state_set_u8(h,"fail_3",2));
  const char payload[]="persisted transaction";
  assert(!launcher_state_set_blob(h,"profile_hash",payload,sizeof(payload)));
  assert(!launcher_state_set_str(h,"source","/sdcard/example.bin"));
  assert(!launcher_state_commit(h));launcher_state_close(h);assert(!storage_io_busy());
  assert(!launcher_state_open("boot_mgr",LAUNCHER_STATE_READONLY,&h));
  int8_t slot;uint8_t count;assert(!launcher_state_get_i8(h,"pending_slot",&slot) && slot==3);
  assert(!launcher_state_get_u8(h,"fail_3",&count) && count==2);
  assert(launcher_state_get_u8(h,"pending_slot",&count)==ESP_ERR_INVALID_STATE);
  char text[80];size_t n=1;
  assert(launcher_state_get_str(h,"source",text,&n)==ESP_ERR_INVALID_SIZE && n==sizeof("/sdcard/example.bin"));
  n=sizeof(text);assert(!launcher_state_get_str(h,"source",text,&n) && !strcmp(text,"/sdcard/example.bin"));
  assert(launcher_state_set_u8(h,"fail_3",99)==ESP_ERR_INVALID_STATE);
  assert(launcher_state_commit(h)==ESP_ERR_INVALID_STATE);launcher_state_close(h);
  // Close without committing leaves the previous record intact.
  assert(!launcher_state_open("boot_mgr",LAUNCHER_STATE_READWRITE,&h));
  assert(!launcher_state_set_u8(h,"fail_3",7));launcher_state_close(h);
  assert(!launcher_state_open("boot_mgr",LAUNCHER_STATE_READONLY,&h));
  assert(!launcher_state_get_u8(h,"fail_3",&count) && count==2);launcher_state_close(h);
  // A failed publication cannot replace the live transaction; a retry works.
  assert(!launcher_state_open("boot_mgr",LAUNCHER_STATE_READWRITE,&h));
  assert(!launcher_state_set_u8(h,"fail_3",7));
  assert(!rename(LAUNCHER_PRIVATE,LAUNCHER_ROOT "/held"));
  FILE*f=fopen(LAUNCHER_PRIVATE,"wb");assert(f);fclose(f);
  assert(launcher_state_commit(h)!=ESP_OK);
  assert(!unlink(LAUNCHER_PRIVATE));assert(!rename(LAUNCHER_ROOT "/held",LAUNCHER_PRIVATE));
  assert(!launcher_state_commit(h));launcher_state_close(h);
  assert(!launcher_state_open("boot_mgr",LAUNCHER_STATE_READONLY,&h));
  assert(!launcher_state_get_u8(h,"fail_3",&count) && count==7);launcher_state_close(h);
  assert(!launcher_state_open("boot_mgr",LAUNCHER_STATE_READWRITE,&h));
  assert(!launcher_state_erase_key(h,"pending_slot"));assert(!launcher_state_commit(h));launcher_state_close(h);
  assert(!launcher_state_open("boot_mgr",LAUNCHER_STATE_READONLY,&h));
  assert(launcher_state_get_i8(h,"pending_slot",&slot)==ESP_ERR_NOT_FOUND);launcher_state_close(h);
  // A valid outer CRC does not make an unknown inner schema valid.
  uint32_t unknown=123;assert(!launcher_store_write("state-damaged",&unknown,sizeof(unknown)));
  assert(launcher_state_open("damaged",LAUNCHER_STATE_READWRITE,&h)==ESP_ERR_INVALID_CRC && !h);
  assert(!storage_io_busy());
  // Corrupt/truncated outer record is never treated as a clean, absent state.
  f=fopen(LAUNCHER_PRIVATE "/state-boot_mgr.dat","wb");assert(f);fputs("cut",f);fclose(f);
  assert(launcher_state_open("boot_mgr",LAUNCHER_STATE_READONLY,&h)!=ESP_OK && !h);
  assert(!storage_io_busy());
  puts("SD state: persistence, type/schema validation, no commit on close, failed publication/retry, writer exclusion, lease cleanup and corrupt journal rejection passed (ASan/UBSan).");
}
