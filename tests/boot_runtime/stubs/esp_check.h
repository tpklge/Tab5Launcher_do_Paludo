#pragma once
#define ESP_RETURN_ON_ERROR(expr,...) do { int e=(expr); if(e) return e; } while(0)
