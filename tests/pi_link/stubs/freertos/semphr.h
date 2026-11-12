#pragma once
#include <stdlib.h>
typedef void *SemaphoreHandle_t;
static inline void *xSemaphoreCreateMutex(void){return (void*)1;}
static inline void *xSemaphoreCreateBinary(void){return (void*)1;}
static inline int xSemaphoreTake(void *p,unsigned t){return 1;}
static inline int xSemaphoreGive(void *p){return 1;}
static inline void vSemaphoreDelete(void *p){}
