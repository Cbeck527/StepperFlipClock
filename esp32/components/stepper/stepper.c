#include "stepper.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_timer.h"

static const char *TAG = "stepper";

static stepper_core_t s_core;
static int s_in_gpio[4];
static esp_timer_handle_t s_timer;

// Runs in the esp_timer task, which runs one callback at a time.
static void tick(void *arg)
{
    uint8_t coils;
    int32_t delay_us = stepper_core_tick(&s_core, esp_timer_get_time(), &coils);
    for (int i = 0; i < 4; i++) {
        gpio_set_level(s_in_gpio[i], (coils >> i) & 1);
    }
    // A one-shot timer armed after the coils change. A late tick can only lengthen the
    // gap before the next step, never shorten it; a periodic timer would catch up on
    // missed periods by firing back to back.
    if (delay_us != STEPPER_STOP) {
        ESP_ERROR_CHECK(esp_timer_start_once(s_timer, (uint64_t)delay_us));
    }
}

esp_err_t stepper_init(const stepper_config_t *config)
{
    uint64_t mask = 0;
    for (int i = 0; i < 4; i++) {
        ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(config->in_gpio[i]), ESP_ERR_INVALID_ARG, TAG,
                            "IN%d: GPIO %d cannot be an output", i + 1, config->in_gpio[i]);
        s_in_gpio[i] = config->in_gpio[i];
        // Set the level before the pin becomes an output, so the coils are never on at boot.
        ESP_RETURN_ON_ERROR(gpio_set_level(s_in_gpio[i], 0), TAG, "IN%d", i + 1);
        mask |= 1ULL << s_in_gpio[i];
    }
    const gpio_config_t io = {.pin_bit_mask = mask, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "configuring the coil GPIOs");

    stepper_core_init(&s_core, config->step_period_us, config->reverse);
    const esp_timer_create_args_t timer = {.callback = tick, .name = "stepper"};
    return esp_timer_create(&timer, &s_timer);
}

void stepper_add(uint32_t steps)
{
    if (stepper_core_add(&s_core, steps)) {
        ESP_ERROR_CHECK(esp_timer_start_once(s_timer, 0));
    }
}

bool stepper_is_busy(void)
{
    return stepper_core_busy(&s_core);
}

stepper_burst_t stepper_last_burst(void)
{
    return s_core.last_burst;
}
