#pragma once

// System time is the clock's single source of truth. This sets it from the DS3231 at
// boot and every resync_minutes after, sets the time zone, and says whether the time
// can be trusted. Everything else reads time() and localtime_r().

#include <stdbool.h>
#include <stdint.h>

#include "ds3231_regs.h"
#include "esp_err.h"

typedef struct {
    const char *timezone;    // POSIX TZ string, such as "EST5EDT,M3.2.0,M11.1.0".
    int sda_gpio;
    int scl_gpio;
    uint32_t resync_minutes; // How often to set system time from the RTC again.
} clock_time_config_t;

// Sets the time zone, then system time from the RTC if the RTC's time can be trusted,
// which takes up to a second. Starts the resync task either way. Returns an error when
// the RTC does not answer; the rest of the firmware can carry on without it.
esp_err_t clock_time_init(const clock_time_config_t *config);

// True once system time has come from an RTC whose oscillator never stopped, or from
// clock_time_set_utc(). It stays true while running, even if the RTC fails later,
// because system time keeps counting on its own.
bool clock_time_trusted(void);

// Sets the RTC and system time to a valid UTC date and time, and clears the RTC's
// oscillator stop flag, so the time is trusted from now on.
esp_err_t clock_time_set_utc(const ds3231_datetime_t *utc);
