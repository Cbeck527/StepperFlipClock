#pragma once

// The clock's one stepper motor, driven through a ULN2003 board. A high GPIO energises a coil.

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "stepper_core.h"

typedef struct {
    int in_gpio[4];          // GPIOs wired to the driver's IN1 to IN4.
    uint32_t step_period_us; // Time between steps.
    bool reverse;            // Turn the other way, for a motor wired in the opposite order.
} stepper_config_t;

// Sets up the GPIOs with all coils off, and the step timer. Call once, before stepper_add().
esp_err_t stepper_init(const stepper_config_t *config);

// The only way to move the motor. Adds steps to the burst in progress, or starts one
// with a step right away. Safe to call from any task.
void stepper_add(uint32_t steps);

// True from stepper_add() until one step period after the last step, when the coils go off.
bool stepper_is_busy(void);

// The last finished burst. Read it once stepper_is_busy() returns false.
stepper_burst_t stepper_last_burst(void);
