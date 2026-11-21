#pragma once
// Host stand-ins: one thread, so the lock does nothing and no task ever runs.
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
