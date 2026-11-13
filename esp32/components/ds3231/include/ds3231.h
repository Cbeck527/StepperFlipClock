#pragma once

// The DS3231 real-time clock on I2C. The clock holds UTC. Single instance.

#include <stdbool.h>
#include <stdint.h>

#include "ds3231_regs.h"
#include "esp_err.h"

// Sets up the I2C bus and checks that the DS3231 answers. Keeps the oscillator running
// on the backup battery and turns off both alarm interrupts, which the Arduino version
// left on.
esp_err_t ds3231_init(int sda_gpio, int scl_gpio);

// Reads the time and the oscillator stop flag in one transaction. *stopped is set
// whenever the read succeeds, even when the time does not decode. Returns
// ESP_ERR_INVALID_RESPONSE when the registers do not hold a valid date and time.
esp_err_t ds3231_read_time(ds3231_datetime_t *t, bool *stopped);

// Writes a valid date and time in one transaction, then clears the oscillator stop flag.
// Returns ESP_ERR_INVALID_ARG, writing nothing, for an invalid date or time.
esp_err_t ds3231_write_time(const ds3231_datetime_t *t);

// Raw access to the alarm 2 registers 0Bh to 0Dh, which the clock uses as battery-backed
// storage. Alarm 2's interrupt stays off, so the values never trigger anything.
esp_err_t ds3231_read_alarm2(uint8_t regs[DS3231_ALARM2_LEN]);
esp_err_t ds3231_write_alarm2(const uint8_t regs[DS3231_ALARM2_LEN]);
