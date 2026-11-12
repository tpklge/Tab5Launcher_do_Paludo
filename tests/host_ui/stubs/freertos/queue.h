#pragma once
#include <stdlib.h>
#include <string.h>
typedef struct { int capacity, size, count, head, tail; unsigned char data[]; } *QueueHandle_t;
static QueueHandle_t xQueueCreate(int n, int size) {
    QueueHandle_t q = calloc(1, sizeof(*q) + n * size);
    q->capacity=n; q->size=size; return q;
}
static int xQueueSend(QueueHandle_t q, const void *item, int timeout) {
    if(q->count==q->capacity) return 0;
    memcpy(q->data+q->tail*q->size,item,q->size);
    q->tail=(q->tail+1)%q->capacity; q->count++; return 1;
}
static int xQueueReceive(QueueHandle_t q, void *item, int timeout) {
    if(!q->count) return 0;
    memcpy(item,q->data+q->head*q->size,q->size);
    q->head=(q->head+1)%q->capacity; q->count--; return 1;
}

static int xQueueOverwrite(QueueHandle_t q,const void *item) { q->head=q->tail=q->count=0; return xQueueSend(q,item,0); }
#ifndef vQueueDelete
#define vQueueDelete free
#endif
