#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef SD_FILES_ROOT
#define SD_FILES_ROOT "/sdcard"
#endif
#define SD_PATH_MAX 512
#define SD_NAME_MAX 256
#define SD_PAGE_SIZE 24
typedef struct { char name[SD_NAME_MAX]; uint64_t size; int64_t modified; bool directory; } sd_entry_t;
typedef struct { sd_entry_t entries[SD_PAGE_SIZE]; unsigned count,total,page; } sd_listing_t;
bool sd_path_valid(const char *path);
bool sd_name_valid(const char *name);
bool sd_path_join(char *out,size_t size,const char *dir,const char *name);
int sd_files_list(const char *path,unsigned page,sd_listing_t *list);
int sd_files_mkdir(const char *dir,const char *name);
int sd_files_rename(const char *path,const char *name);
int sd_files_delete(const char *path);
typedef bool (*sd_copy_progress_fn)(unsigned percent,void *user);
int sd_files_copy_progress(const char *source,const char *directory,sd_copy_progress_fn progress,void *user);
int sd_files_copy(const char *source,const char *directory);
int sd_files_preview(const char *path,char *text,size_t size,bool *truncated);

/* Atomic text replacement, rejects changed/truncated originals. Limit 4095 bytes. */
int sd_files_edit(const char *path,const char *expected,const char *text);

int sd_files_list_filtered(const char *path,unsigned page,sd_listing_t *list,const char *focus,bool images);

/* Caller frees entries; same sorted order and 16384 entry limit as UI. */
int sd_files_list_all(const char *,sd_entry_t **,unsigned *,bool images);
int sd_files_delete_recursive(const char *);

#ifndef USB_FILES_ROOT
#ifndef USB_FILES_ROOT
#define USB_FILES_ROOT "/usb"
#endif
#endif
const char *sd_path_root(const char *);
bool sd_media_acquire(const char *);
void sd_media_release(const char *);
void sd_usb_set_available(bool);
unsigned sd_usb_active_operations(void);
