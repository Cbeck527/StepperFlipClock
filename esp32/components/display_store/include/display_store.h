#pragma once

// Saves what the display shows in the DS3231, in the format of display_store_format.h.
// Call ds3231_init() first.

#include "display_store_format.h"
#include "esp_err.h"

// Reads the saved record. *status says what it holds.
esp_err_t display_store_load(uint32_t minutes_per_cycle, display_store_status_t *status,
                             display_record_t *record);

// Marks the display as changing. Call before anything moves it.
esp_err_t display_store_begin(uint32_t cycle_minute);

// Saves a complete record and clears the changing mark, in one write. Call after
// display_store_begin(), once the display shows record->display.
esp_err_t display_store_commit(const display_record_t *record);
