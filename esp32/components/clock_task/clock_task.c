#include "clock_task.h"

#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "clock_time.h"
#include "display_store.h"
#include "esp_check.h"
#include "esp_log.h"
#include "flip_logic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "stepper.h"

static const char *TAG = "clock_task";

#define NO_ACTION ((flip_action_t)-1)

// Everything below is guarded by s_lock, which the task holds for each 10 ms pass.
static SemaphoreHandle_t s_lock;
static flip_clock_t s_clock;
static bool s_known; // The display shows s_clock.display, so the clock may move it.
static display_store_status_t s_saved_status; // What the DS3231 held at boot.
static display_record_t s_saved;
static bool s_load_failed;
static flip_action_t s_action = NO_ACTION; // The last decision, for logging changes.
static int s_target = FLIP_NO_TARGET;
static uint32_t s_run_flips, s_run_steps;
static bool s_store_failing; // The last write to the DS3231 failed, so the display is stopped.

#define HHMM_LEN 8 // "HH:MM" and room the compiler can prove is enough.

// Writes a minute of day as HH:MM, wrapping past midnight.
static void format_minute(char text[HHMM_LEN], int minute)
{
    unsigned m = (unsigned)((minute % FLIP_MINUTES_PER_DAY + FLIP_MINUTES_PER_DAY) % FLIP_MINUTES_PER_DAY);
    snprintf(text, HHMM_LEN, "%02u:%02u", m / 60 % 24, m % 60);
}

static int local_minute_of_day(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    return tm.tm_hour * 60 + tm.tm_min;
}

// The minute the display should show: local time, or none when the clock must not move.
static int target(void)
{
    return s_known && clock_time_trusted() ? local_minute_of_day() : FLIP_NO_TARGET;
}

// Logs why the clock moves or waits, once each time that changes, and each run of flips.
static void report(flip_action_t action)
{
    char display[HHMM_LEN], goal[HHMM_LEN];
    format_minute(display, s_clock.display);
    format_minute(goal, s_target);
    if (action == FLIP_ON_TIME && s_run_flips > 0) {
        ESP_LOGI(TAG, "Display %s after %u flip%s, %u steps", display, (unsigned)s_run_flips,
                 s_run_flips == 1 ? "" : "s", (unsigned)s_run_steps);
        s_run_flips = s_run_steps = 0;
    }
    if (action == s_action) {
        return;
    }
    s_action = action;
    int behind = (s_target - s_clock.display + FLIP_MINUTES_PER_DAY) % FLIP_MINUTES_PER_DAY;
    switch (action) {
    case FLIP_ADVANCE:
        if (behind > 1) { // One minute behind is every minute's normal flip.
            ESP_LOGI(TAG, "Display %s is %d minutes behind %s: catching up", display, behind, goal);
        }
        break;
    case FLIP_WIND_AROUND:
        ESP_LOGW(TAG, "Display %s is %d minutes ahead of %s, more than the %u-minute pause: "
                 "winding forward %d minutes",
                 display, FLIP_MINUTES_PER_DAY - behind, goal, (unsigned)s_clock.max_pause_minutes, behind);
        break;
    case FLIP_PAUSE:
        ESP_LOGI(TAG, "Display %s is %d minutes ahead of %s: waiting for the time to catch up",
                 display, FLIP_MINUTES_PER_DAY - behind, goal);
        break;
    case FLIP_IDLE:
        ESP_LOGI(TAG, "Not moving the display: %s",
                 s_known ? "the time is not trusted" : "what it shows is unknown; set it with w HHMM");
        break;
    case FLIP_ON_TIME:
        break;
    }
}

// Logs the first of a run of failed writes to the DS3231. Only a successful changing mark
// ends the run, because that is the write the display waits for.
static bool saved(esp_err_t err)
{
    if (err != ESP_OK && !s_store_failing) {
        ESP_LOGE(TAG, "Saving the display in the RTC failed (%s): the display stops until the "
                 "RTC answers again", esp_err_to_name(err));
    }
    s_store_failing |= err != ESP_OK;
    return err == ESP_OK;
}

static void store_working(void)
{
    if (s_store_failing) {
        ESP_LOGI(TAG, "Saving the display in the RTC works again");
        s_store_failing = false;
    }
}

// One pass with the motor idle: save the minute the last burst finished, then decide the next.
static void pass(void)
{
    // Kept so that a failed write can undo this pass and the next one can try again.
    const flip_clock_t before = s_clock;
    if (s_clock.advancing) {
        // The burst is over, so the display shows the next minute. flip_clock_update() counts it.
        // A failed commit is safe to carry on from: the saved record still says "changing".
        const display_record_t done = {
            .display = (s_clock.display + 1) % FLIP_MINUTES_PER_DAY,
            .cycle_minute = s_clock.cycle.minute,
        };
        saved(display_store_commit(&done));
    }
    s_target = target();
    flip_clock_set_target(&s_clock, s_target);
    flip_next_t next = flip_clock_update(&s_clock);
    if (next.steps > 0) {
        // Nothing moves until the saved record says the display is changing. Otherwise a power
        // cut during the burst would leave a record of the old minute that looks valid.
        if (!saved(display_store_begin(s_clock.cycle.minute))) {
            s_clock = before;
            return;
        }
        store_working();
        stepper_add(next.steps);
        s_run_flips++;
        s_run_steps += next.steps;
    }
    report(next.action);
}

static void clock_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        // flip_clock_update() may only run with the motor idle; s also moves it.
        if (!stepper_is_busy()) {
            pass();
        }
        xSemaphoreGive(s_lock);
    }
}

esp_err_t clock_task_start(const clock_task_config_t *config)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "creating the lock");
    flip_clock_init(&s_clock, config->steps_per_cycle, config->minutes_per_cycle,
                    config->max_pause_minutes, 0);

    char saved[HHMM_LEN];
    esp_err_t err = display_store_load(config->minutes_per_cycle, &s_saved_status, &s_saved);
    s_load_failed = err != ESP_OK;
    format_minute(saved, s_saved.display);
    if (s_load_failed) {
        ESP_LOGW(TAG, "Could not read the saved display (%s)", esp_err_to_name(err));
    } else if (s_saved_status == DISPLAY_STORE_KNOWN) {
        flip_clock_set_display(&s_clock, s_saved.display);
        flip_cycle_set_minute(&s_clock.cycle, s_saved.cycle_minute);
        s_known = true;
        ESP_LOGI(TAG, "Display %s, minute %u of the cycle, as saved", saved, (unsigned)s_saved.cycle_minute);
    } else if (s_saved_status == DISPLAY_STORE_CHANGING) {
        char next[HHMM_LEN];
        format_minute(next, s_saved.display + 1);
        ESP_LOGW(TAG, "Power was lost while the display was changing from %s to %s", saved, next);
    } else if (s_saved_status == DISPLAY_STORE_ARDUINO) {
        ESP_LOGW(TAG, "The saved display, %s, is from the Arduino version and probably stale", saved);
    } else {
        ESP_LOGW(TAG, "No saved display");
    }
    if (!s_known) {
        ESP_LOGW(TAG, "Check what the display shows, then set it with: w HHMM");
    }

    ESP_RETURN_ON_FALSE(xTaskCreate(clock_task, "clock", 4096, NULL, 4, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "starting the clock task");
    return ESP_OK;
}

esp_err_t clock_task_set_display(int minute_of_day)
{
    ESP_RETURN_ON_FALSE(minute_of_day >= 0 && minute_of_day < FLIP_MINUTES_PER_DAY,
                        ESP_ERR_INVALID_ARG, TAG, "minute of day out of range");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    // Holding the lock stops the task from starting another burst while this one finishes.
    while (stepper_is_busy()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // The person's reading replaces the count of a burst that has just finished.
    s_clock.advancing = false;
    flip_clock_set_display(&s_clock, minute_of_day);
    const display_record_t record = {.display = minute_of_day, .cycle_minute = s_clock.cycle.minute};
    esp_err_t err = display_store_begin(record.cycle_minute);
    if (err == ESP_OK) {
        err = display_store_commit(&record);
    }
    if (err == ESP_OK) {
        store_working();
    }
    s_known = true;
    s_action = NO_ACTION; // Log the next decision whatever it is.
    s_run_flips = s_run_steps = 0;
    xSemaphoreGive(s_lock);
    return err;
}

void clock_task_describe(char *text, size_t size)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    char display[HHMM_LEN], goal[HHMM_LEN];
    format_minute(display, s_clock.display);
    format_minute(goal, s_target);
    if (!s_known) {
        format_minute(goal, s_saved.display + 1);
        if (s_load_failed) {
            snprintf(text, size, "Display unknown: the RTC did not answer at boot. Set it with: w HHMM");
        } else if (s_saved_status == DISPLAY_STORE_CHANGING) {
            char saved[HHMM_LEN];
            format_minute(saved, s_saved.display);
            snprintf(text, size, "Display unknown: power was lost while it changed from %s to %s. "
                     "Set it with: w HHMM", saved, goal);
        } else if (s_saved_status == DISPLAY_STORE_ARDUINO) {
            char saved[HHMM_LEN];
            format_minute(saved, s_saved.display);
            snprintf(text, size, "Display unknown: %s was saved by the Arduino version. "
                     "Set it with: w HHMM", saved);
        } else {
            snprintf(text, size, "Display unknown: nothing usable is saved. Set it with: w HHMM");
        }
    } else if (s_store_failing) {
        snprintf(text, size, "Display %s, stopped: saving it in the RTC fails", display);
    } else if (s_target == FLIP_NO_TARGET) {
        snprintf(text, size, "Display %s, not moving: the time is not trusted", display);
    } else if (s_action == FLIP_PAUSE) {
        snprintf(text, size, "Display %s, waiting for local time %s to catch up", display, goal);
    } else if (s_action == FLIP_WIND_AROUND || (s_action == FLIP_ADVANCE && s_run_flips > 1)) {
        snprintf(text, size, "Display %s, moving forward to %s", display, goal);
    } else {
        snprintf(text, size, "Display %s, following local time", display);
    }
    xSemaphoreGive(s_lock);
}
