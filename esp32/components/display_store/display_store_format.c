#include "display_store_format.h"

#define MASK_BIT 0x80     // Alarm 2 mask bit in each register: set in this firmware's records.
#define CHANGING_BIT 0x40 // 0Dh bit 6, the DY/DT bit, which the per-minute alarm ignores.
#define CYCLE_BITS 0x3f

// The Arduino version's 0Dh: day 1, DY/DT and mask bits clear (setA2Time(1, h, m, 0, ...)).
#define ARDUINO_DAY 0x01

static uint8_t to_bcd(int value)
{
    return (uint8_t)((value / 10) << 4 | value % 10);
}

// Decodes BCD, or returns -1 when the low digit is above 9. A high digit above 9 gives 100 or
// more, which the range checks in decode_display() refuse.
static int from_bcd(uint8_t value)
{
    if ((value & 0x0f) > 9) {
        return -1;
    }
    return (value >> 4) * 10 + (value & 0x0f);
}

void display_store_encode(const display_record_t *record, uint8_t regs[DISPLAY_STORE_LEN])
{
    regs[0] = MASK_BIT | to_bcd(record->display % 60);
    regs[1] = MASK_BIT | to_bcd(record->display / 60);
    regs[2] = MASK_BIT | (uint8_t)record->cycle_minute;
}

uint8_t display_store_changing_byte(uint32_t cycle_minute)
{
    return MASK_BIT | CHANGING_BIT | (uint8_t)(cycle_minute & CYCLE_BITS);
}

// Minute of day from the minute and hour registers with their mask bits removed, or -1.
static int decode_display(uint8_t minute_reg, uint8_t hour_reg)
{
    int minute = from_bcd(minute_reg);
    int hour = from_bcd(hour_reg); // A set 12-hour bit makes this 40 or more.
    if (minute < 0 || minute > 59 || hour < 0 || hour > 23) {
        return -1;
    }
    return hour * 60 + minute;
}

display_store_status_t display_store_decode(const uint8_t regs[DISPLAY_STORE_LEN],
                                            uint32_t minutes_per_cycle, display_record_t *record)
{
    uint8_t masks = regs[0] & regs[1] & regs[2] & MASK_BIT;
    uint8_t any_mask = (regs[0] | regs[1] | regs[2]) & MASK_BIT;

    if (masks) {
        int display = decode_display(regs[0] & ~MASK_BIT, regs[1] & ~MASK_BIT);
        uint32_t cycle_minute = regs[2] & CYCLE_BITS;
        if (display < 0 || cycle_minute >= minutes_per_cycle) {
            return DISPLAY_STORE_INVALID;
        }
        *record = (display_record_t){.display = display, .cycle_minute = cycle_minute};
        return regs[2] & CHANGING_BIT ? DISPLAY_STORE_CHANGING : DISPLAY_STORE_KNOWN;
    }
    if (!any_mask && regs[2] == ARDUINO_DAY) {
        int display = decode_display(regs[0], regs[1]);
        if (display < 0) {
            return DISPLAY_STORE_INVALID;
        }
        *record = (display_record_t){.display = display, .cycle_minute = 0};
        return DISPLAY_STORE_ARDUINO;
    }
    return DISPLAY_STORE_INVALID;
}
