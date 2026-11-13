#include "console_cmds.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "flip_logic.h"
#include "sdkconfig.h"
#include "stepper.h"

static const char *TAG = "flipclock";

// An unset bool option is not defined at all.
#ifdef CONFIG_FLIPCLOCK_REVERSE
#define REVERSE true
#else
#define REVERSE false
#endif

void app_main(void)
{
    const stepper_config_t stepper = {
        .in_gpio = {CONFIG_FLIPCLOCK_IN1_GPIO, CONFIG_FLIPCLOCK_IN2_GPIO,
                    CONFIG_FLIPCLOCK_IN3_GPIO, CONFIG_FLIPCLOCK_IN4_GPIO},
        .step_period_us = CONFIG_FLIPCLOCK_STEP_PERIOD_US,
        .reverse = REVERSE,
    };
    ESP_ERROR_CHECK(stepper_init(&stepper));
    ESP_LOGI(TAG, "Stepper on GPIO %d, %d, %d, %d (IN1-IN4), %u us per step%s",
             stepper.in_gpio[0], stepper.in_gpio[1], stepper.in_gpio[2], stepper.in_gpio[3],
             (unsigned)stepper.step_period_us, stepper.reverse ? ", reversed" : "");

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
    // The s command can turn the motor at most one full cycle at a time.
    ESP_ERROR_CHECK(console_cmds_register(CONFIG_FLIPCLOCK_STEPS_PER_CYCLE, stepper.step_period_us));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
