#pragma once
#include "pi_link.h"
typedef bool (*pi_service_progress_fn)(unsigned,const char *,void *);
esp_err_t pi_services_list(bool backups,char *text,size_t capacity);
esp_err_t pi_services_fetch(const char *where,const char *name,char *path,size_t size,pi_service_progress_fn,void*);
esp_err_t pi_services_backup(const char *local,pi_service_progress_fn,void*);
