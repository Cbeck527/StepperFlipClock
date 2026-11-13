#include "clock_time.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "ds3231.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "clock_time";

static atomic_bool s_trusted;
static SemaphoreHandle_t s_lock; // Keeps a resync from undoing clock_time_set_utc().
static uint32_t s_resync_minutes;

static void log_time(const char *what, time_t now)
{
    struct tm utc, local;
    gmtime_r(&now, &utc);
    localtime_r(&now, &local);
    char utc_text[24], local_text[32];
    strftime(utc_text, sizeof utc_text, "%Y-%m-%d %H:%M:%S", &utc);
    strftime(local_text, sizeof local_text, "%Y-%m-%d %H:%M:%S %Z", &local);
    ESP_LOGI(TAG, "%s: %s UTC, local %s", what, utc_text, local_text);
}

// Reads the RTC just after its seconds register changes, which is the start of a second,
// so system time gets the RTC's time to within one FreeRTOS tick (10 ms) instead of
// within a second. Fails when the oscillator stop flag is set or the seconds do not count.
static esp_err_t read_rtc_at_second_edge(int64_t *unix_seconds)
{
    ds3231_datetime_t before, t;
    bool stopped;
    ESP_RETURN_ON_ERROR(ds3231_read_time(&before, &stopped), TAG, "RTC time unreadable");
    ESP_RETURN_ON_FALSE(!stopped, ESP_ERR_INVALID_STATE, TAG,
                        "RTC oscillator stop flag is set: the backup battery is flat or was out");
    TickType_t start = xTaskGetTickCount();
    do {
        vTaskDelay(1);
        ESP_RETURN_ON_ERROR(ds3231_read_time(&t, &stopped), TAG, "RTC time unreadable");
        if (t.second != before.second) {
            *unix_seconds = ds3231_datetime_to_unix(&t);
            return ESP_OK;
        }
    } while (xTaskGetTickCount() - start < pdMS_TO_TICKS(1500));
    ESP_LOGE(TAG, "RTC seconds are not counting");
    return ESP_ERR_TIMEOUT;
}

// Sets system time from the RTC. Call with s_lock held.
static esp_err_t sync_from_rtc(void)
{
    int64_t rtc;
    ESP_RETURN_ON_ERROR(read_rtc_at_second_edge(&rtc), TAG, "not setting system time");
    struct timeval before;
    gettimeofday(&before, NULL);
    const struct timeval tv = {.tv_sec = (time_t)rtc, .tv_usec = 0};
    settimeofday(&tv, NULL);
    if (atomic_exchange(&s_trusted, true)) {
        int64_t drift_ms = ((int64_t)before.tv_sec - rtc) * 1000 + before.tv_usec / 1000;
        ESP_LOGI(TAG, "Resynced from the RTC; system time was %+lld ms off", (long long)drift_ms);
    } else {
        log_time("System time set from the RTC", tv.tv_sec);
    }
    return ESP_OK;
}

static void resync_task(void *arg)
{
    for (;;) {
        for (uint32_t i = 0; i < s_resync_minutes; i++) {
            vTaskDelay(pdMS_TO_TICKS(60 * 1000));
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (sync_from_rtc() != ESP_OK && atomic_load(&s_trusted)) {
            ESP_LOGW(TAG, "Keeping system time, which is still trusted");
        }
        xSemaphoreGive(s_lock);
    }
}

esp_err_t clock_time_init(const clock_time_config_t *config)
{
    setenv("TZ", config->timezone, 1);
    tzset();
    s_resync_minutes = config->resync_minutes;
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "creating the lock");

    esp_err_t err = ds3231_init(config->sda_gpio, config->scl_gpio);
    if (err == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        err = sync_from_rtc();
        xSemaphoreGive(s_lock);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Time not trusted. Set it with: z <yyyymmddhhmmss>, in UTC");
    }
    // The resync task also picks up an RTC whose time was unreadable at boot. An RTC that
    // did not answer at all needs a restart once the wiring is fixed.
    ESP_RETURN_ON_FALSE(xTaskCreate(resync_task, "clock_time", 3072, NULL, 1, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "starting the resync task");
    return err;
}

bool clock_time_trusted(void)
{
    return atomic_load(&s_trusted);
}

esp_err_t clock_time_set_utc(const ds3231_datetime_t *utc)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    // Writing the seconds register restarts the RTC's second, so both clocks start it now.
    esp_err_t err = ds3231_write_time(utc);
    if (err == ESP_OK) {
        const struct timeval tv = {.tv_sec = (time_t)ds3231_datetime_to_unix(utc), .tv_usec = 0};
        settimeofday(&tv, NULL);
        atomic_store(&s_trusted, true);
        log_time("Time set", tv.tv_sec);
    }
    xSemaphoreGive(s_lock);
    return err;
}
