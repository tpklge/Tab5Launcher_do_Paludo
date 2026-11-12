#pragma once
typedef void *SemaphoreHandle_t;
static inline void *xSemaphoreCreateMutex(void) { return (void*)1; }
#define xSemaphoreTake(...) ((void)0)
#define xSemaphoreGive(...) ((void)0)
