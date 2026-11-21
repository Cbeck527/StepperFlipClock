#pragma once
#include "esp_err.h"
#define ESP_RETURN_ON_FALSE(cond, err, tag, ...) \
    do {                                          \
        if (!(cond)) {                            \
            return err;                           \
        }                                         \
    } while (0)
