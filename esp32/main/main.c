#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "flip_logic.h"
#include "sdkconfig.h"

static const char *TAG = "flipclock";

void app_main(void)
{
    // There is no display store yet, so assume the display shows 00:00.
    flip_clock_t clock;
    flip_clock_init(&clock, CONFIG_FLIPCLOCK_STEPS_PER_CYCLE, CONFIG_FLIPCLOCK_MINUTES_PER_CYCLE,
                    CONFIG_FLIPCLOCK_MAX_PAUSE_MINUTES, 0);
    ESP_LOGI(TAG, "Motor: %u steps per %u minutes; pauses up to %u minutes",
             (unsigned)clock.cycle.steps_per_cycle, (unsigned)clock.cycle.minutes_per_cycle,
             (unsigned)clock.max_pause_minutes);

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "flipclock>";

    // Uses the console selected in sdkconfig, which sdkconfig.defaults sets to USB Serial/JTAG.
    // It also registers the help command.
    ESP_ERROR_CHECK(esp_console_new_repl_stdio(&repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
