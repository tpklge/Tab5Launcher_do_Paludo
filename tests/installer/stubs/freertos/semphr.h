#pragma once
typedef void *SemaphoreHandle_t;
extern int test_lock;
static inline void *xSemaphoreCreateMutex(void) { return (void*)1; }
static inline int xSemaphoreTake(void *lock,unsigned ticks) { if(!test_lock) return 0; test_lock=0; return 1; }
static inline void xSemaphoreGive(void *lock) { test_lock=1; }
