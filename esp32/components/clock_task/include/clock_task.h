#pragma once

// The clock itself: a task that every 10 ms drives the display towards local time, one
// minute per motor burst, and saves what the display shows in the DS3231. It moves the
// display only when the time is trusted and it knows what the display shows.

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t steps_per_cycle;
    uint32_t minutes_per_cycle;
    uint32_t max_pause_minutes;
} clock_task_config_t;

// Loads the saved display from the DS3231 and starts the task. Call after stepper_init()
// and clock_time_init(). Without a usable saved display, the clock waits for
// clock_task_set_display().
esp_err_t clock_task_start(const clock_task_config_t *config);

// Records what the display shows, minute of day 0 to 1439, after someone reads it. Waits for
// a burst in progress to finish first. From then on the clock moves it when needed.
esp_err_t clock_task_set_display(int minute_of_day);

// Writes one line saying what the display shows and what the clock is doing with it.
void clock_task_describe(char *text, size_t size);
