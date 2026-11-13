#include "console_cmds.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_console.h"
#include "flip_logic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stepper.h"

static const char *TAG = "console_cmds";

static uint32_t s_max_steps;
static uint32_t s_step_period_us;

static int cmd_step(int argc, char **argv)
{
    uint32_t count;
    if (argc != 2 || !flip_parse_uint(argv[1], 1, s_max_steps, &count)) {
        printf("Usage: s <count>, with count from 1 to %" PRIu32 "\n", s_max_steps);
        return 1;
    }

    stepper_add(count);
    // Wait for the burst and the coil switch-off after it, allowing twice the expected time.
    TickType_t limit = xTaskGetTickCount() +
                       pdMS_TO_TICKS(2 * (uint64_t)(count + 1) * s_step_period_us / 1000 + 1000);
    while (stepper_is_busy()) {
        if (xTaskGetTickCount() > limit) {
            printf("s %" PRIu32 ": the motor is still running, which should not happen\n", count);
            return 1;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    stepper_burst_t burst = stepper_last_burst();
    printf("s %" PRIu32 ": %" PRIu32 " steps in %" PRIu32 " ms, gap min %" PRIu32 " us, max %" PRIu32 " us\n",
           count, burst.steps, burst.span_us / 1000, burst.min_gap_us, burst.max_gap_us);
    return 0;
}

esp_err_t console_cmds_register(uint32_t max_steps, uint32_t step_period_us)
{
    s_max_steps = max_steps;
    s_step_period_us = step_period_us;
    const esp_console_cmd_t step = {
        .command = "s",
        .help = "Turn the motor <count> steps, bypassing the clock. 142 or 143 steps is one minute on "
                "the Copal 227. Prints the steps taken and the shortest and longest gap between steps.",
        .hint = "<count>",
        .func = cmd_step,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&step), TAG, "registering s");
    return ESP_OK;
}
