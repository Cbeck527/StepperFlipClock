#pragma once

#include <stdint.h>

#include "esp_err.h"

// Registers the clock's console commands. Call after the console REPL is created.
// max_steps caps the s command; step_period_us is the stepper's time between steps.
esp_err_t console_cmds_register(uint32_t max_steps, uint32_t step_period_us);
