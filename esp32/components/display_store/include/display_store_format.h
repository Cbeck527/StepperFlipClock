#pragma once

// How the clock saves what its display shows in the DS3231's alarm 2 registers, 0Bh to 0Dh,
// which are battery-backed and have no write limit. Plain C with no ESP-IDF headers:
// host_test/ builds this on the host. display_store.c moves the bytes.
//
//   0Bh  bit 7 = 1   bits 6-0 = display minute, BCD 00-59
//   0Ch  bit 7 = 1   bit 6 = 0 (24-hour)   bits 5-0 = display hour, BCD 00-23
//   0Dh  bit 7 = 1   bit 6 = 1 while the display is changing   bits 5-0 = minute in the cycle
//
// Bit 7 of each register is an alarm 2 mask bit. All three set is the datasheet's "alarm
// once per minute" setting (Table 2), so the bytes stay a configuration the DS3231 defines,
// and they mark the record as this firmware's: the Arduino version wrote all three as 0.
// Alarm 2's interrupt stays off, so the alarm never does anything.
//
// Saving is two writes of all three bytes. Before the display changes, the first sets the
// changing bit and rewrites 0Bh and 0Ch unchanged. After it has changed, the second stores
// the new display with the bit clear. The DS3231 stores each byte as it arrives and 0Dh
// comes last, so a power cut part-way through either write leaves the changing bit set,
// or the old record intact.

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_STORE_LEN 3

typedef struct {
    int display;           // Minute of day the display shows, 0 to 1439.
    uint32_t cycle_minute; // flip_cycle_t.minute: position in the mechanism's turn.
} display_record_t;

typedef enum {
    DISPLAY_STORE_KNOWN,    // A complete record: the display shows record.display.
    DISPLAY_STORE_CHANGING, // Power was lost while the display was changing, so it shows
                            // record.display, a later minute, or something in between.
    DISPLAY_STORE_ARDUINO,  // What the Arduino version saved: record.display, with no cycle
                            // position. Probably stale.
    DISPLAY_STORE_INVALID,  // Nothing usable.
} display_store_status_t;

// The three bytes of a complete record. record must be valid for minutes_per_cycle.
void display_store_encode(const display_record_t *record, uint8_t regs[DISPLAY_STORE_LEN]);

// The 0Dh byte that marks the display as changing, written on its own before a change.
uint8_t display_store_changing_byte(uint32_t cycle_minute);

// Reads the three bytes. Sets *record for KNOWN, CHANGING and ARDUINO, and leaves it alone
// for INVALID. A cycle minute of minutes_per_cycle or more is INVALID.
display_store_status_t display_store_decode(const uint8_t regs[DISPLAY_STORE_LEN],
                                            uint32_t minutes_per_cycle, display_record_t *record);
