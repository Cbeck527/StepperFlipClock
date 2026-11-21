#pragma once
#include "freertos/FreeRTOS.h"
typedef int *SemaphoreHandle_t;
static int stub_mutex;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &stub_mutex; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t lock, TickType_t ticks)
{
    (void)lock;
    (void)ticks;
    return pdPASS;
}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t lock)
{
    (void)lock;
    return pdPASS;
}
