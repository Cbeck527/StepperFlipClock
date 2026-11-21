#pragma once
#include "freertos/FreeRTOS.h"
#define xTaskCreate(fn, name, stack, arg, prio, handle) ((void)(fn), pdPASS)
#define xTaskGetTickCount() ((TickType_t)0)
#define vTaskDelay(ticks) ((void)(ticks))
#define vTaskDelayUntil(wake, ticks) ((void)(wake), (void)(ticks))
