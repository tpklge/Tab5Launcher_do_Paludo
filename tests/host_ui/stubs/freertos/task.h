#pragma once
typedef void *TaskHandle_t;
static void vTaskDelay(unsigned ticks) {}
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,int priority,TaskHandle_t *handle) { if(handle) *handle=(void*)1; return 1; }
