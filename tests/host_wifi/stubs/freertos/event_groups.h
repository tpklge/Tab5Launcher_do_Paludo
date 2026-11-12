#pragma once
typedef void *EventGroupHandle_t;
typedef unsigned EventBits_t;
unsigned xEventGroupClearBits(EventGroupHandle_t, unsigned);
unsigned xEventGroupWaitBits(EventGroupHandle_t, unsigned, int, int, unsigned);
