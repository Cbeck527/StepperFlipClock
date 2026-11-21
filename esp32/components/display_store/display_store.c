#include "display_store.h"

#include "ds3231.h"

_Static_assert(DISPLAY_STORE_LEN == DS3231_ALARM2_LEN, "the record fills the alarm 2 registers");

esp_err_t display_store_load(uint32_t minutes_per_cycle, display_store_status_t *status,
                             display_record_t *record)
{
    uint8_t regs[DISPLAY_STORE_LEN];
    esp_err_t err = ds3231_read_alarm2(regs);
    if (err == ESP_OK) {
        *status = display_store_decode(regs, minutes_per_cycle, record);
    }
    return err;
}

esp_err_t display_store_begin(uint32_t cycle_minute)
{
    // Rewrites 0Bh and 0Ch with what they hold, so only 0Dh changes, in one transaction.
    uint8_t regs[DISPLAY_STORE_LEN];
    esp_err_t err = ds3231_read_alarm2(regs);
    if (err != ESP_OK) {
        return err;
    }
    regs[2] = display_store_changing_byte(cycle_minute);
    return ds3231_write_alarm2(regs);
}

esp_err_t display_store_commit(const display_record_t *record)
{
    uint8_t regs[DISPLAY_STORE_LEN];
    display_store_encode(record, regs);
    return ds3231_write_alarm2(regs);
}
