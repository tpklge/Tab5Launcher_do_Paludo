#pragma once
typedef void *QueueHandle_t;
#ifndef PI_QUEUE_CAPTURE
static inline void *xQueueCreate(unsigned a,unsigned b){return (void*)1;}
static inline int xQueueSend(void *q,void *p,unsigned t){return 1;}
static inline int xQueueReceive(void *q,void *p,unsigned t){return 0;}

#else
#include <string.h>
#include <assert.h>
static unsigned char queue_data[2048];static unsigned queue_size;static int queue_full;
static inline void *xQueueCreate(unsigned a,unsigned b){assert(b<=sizeof(queue_data));queue_size=b;queue_full=0;return (void*)1;}
static inline int xQueueSend(void *q,void *p,unsigned t){if(queue_full)return 0;memcpy(queue_data,p,queue_size);queue_full=1;return 1;}
static inline int xQueueReceive(void *q,void *p,unsigned t){if(!queue_full)return 0;memcpy(p,queue_data,queue_size);queue_full=0;return 1;}
#endif
