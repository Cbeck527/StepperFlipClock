#pragma once

#include <stdint.h>

#include "esp_err.h"

// Registers the clock's console commands. Call after the console REPL is created.
// max_steps caps the s command; step_period_us is the stepper's time between steps.
esp_err_t console_cmds_register(uint32_t max_steps, uint32_t step_period_us);

// Works around ESP-IDF v6.1 issue 19366: with the console on USB Serial/JTAG, the REPL's
// check for a terminal that understands escape sequences always fails, which turns off line
// editing and history. Call between esp_console_new_repl_stdio() and esp_console_start_repl().
void console_cmds_redo_terminal_probe(void);
