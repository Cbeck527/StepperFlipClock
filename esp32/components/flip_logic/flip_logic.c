#include "flip_logic.h"

#include <stddef.h>

void flip_cycle_init(flip_cycle_t *cycle, uint32_t steps_per_cycle, uint32_t minutes_per_cycle)
{
    *cycle = (flip_cycle_t){
        .steps_per_cycle = steps_per_cycle,
        .minutes_per_cycle = minutes_per_cycle,
    };
}

// Ported from advance_one_minute() in BurstClockSerialDS3231_copal227.ino.
uint32_t flip_cycle_advance(flip_cycle_t *cycle)
{
    cycle->minute++;
    // Where the motor should be after this minute, rounded down. Measuring from the start
    // of the cycle, not from the last minute, keeps rounding error from accumulating.
    uint32_t target = cycle->minute * cycle->steps_per_cycle / cycle->minutes_per_cycle;
    uint32_t steps = target - cycle->step;
    cycle->minute %= cycle->minutes_per_cycle;
    cycle->step = target % cycle->steps_per_cycle;
    return steps;
}

static bool is_minute_of_day(int minute)
{
    return minute >= 0 && minute < FLIP_MINUTES_PER_DAY;
}

void flip_clock_init(flip_clock_t *clock, uint32_t steps_per_cycle, uint32_t minutes_per_cycle,
                     uint32_t max_pause_minutes, int display)
{
    *clock = (flip_clock_t){
        .max_pause_minutes = max_pause_minutes,
        .display = display,
        .target = FLIP_NO_TARGET,
    };
    flip_cycle_init(&clock->cycle, steps_per_cycle, minutes_per_cycle);
}

bool flip_clock_set_target(flip_clock_t *clock, int minute_of_day)
{
    if (minute_of_day != FLIP_NO_TARGET && !is_minute_of_day(minute_of_day)) {
        return false;
    }
    clock->target = minute_of_day;
    return true;
}

bool flip_clock_set_display(flip_clock_t *clock, int minute_of_day)
{
    if (!is_minute_of_day(minute_of_day)) {
        return false;
    }
    clock->display = minute_of_day;
    return true;
}

// Ported from flip_update() in BurstClockSerialDS3231_copal227.ino.
flip_next_t flip_clock_update(flip_clock_t *clock)
{
    if (clock->advancing) {
        clock->advancing = false;
        clock->display = (clock->display + 1) % FLIP_MINUTES_PER_DAY;
    }
    if (clock->target == FLIP_NO_TARGET) {
        return (flip_next_t){.action = FLIP_IDLE};
    }
    if (clock->display == clock->target) {
        return (flip_next_t){.action = FLIP_ON_TIME};
    }

    // Minutes to advance to reach the target, 1 to 1439. The display is "ahead" by the rest of the day.
    int behind = (clock->target - clock->display + FLIP_MINUTES_PER_DAY) % FLIP_MINUTES_PER_DAY;
    int ahead = FLIP_MINUTES_PER_DAY - behind;
    if (ahead <= (int)clock->max_pause_minutes) {
        return (flip_next_t){.action = FLIP_PAUSE};
    }
    // Both advance one minute; the action only tells the caller why.
    clock->advancing = true;
    return (flip_next_t){
        .action = behind <= FLIP_MINUTES_PER_DAY / 2 ? FLIP_ADVANCE : FLIP_WIND_AROUND,
        .steps = flip_cycle_advance(&clock->cycle),
    };
}

// Ported from parse_string_to_mins() in BurstClockSerialDS3231_copal227.ino, with range checks.
bool flip_parse_hhmm(const char *text, int *minute_of_day)
{
    int digits[4];
    for (size_t i = 0; i < 4; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false; // Also stops at a terminating '\0' before the fourth digit.
        }
        digits[i] = text[i] - '0';
    }
    if (text[4] != '\0') {
        return false;
    }
    int hours = 10 * digits[0] + digits[1];
    int minutes = 10 * digits[2] + digits[3];
    if (hours > 23 || minutes > 59) {
        return false;
    }
    *minute_of_day = 60 * hours + minutes;
    return true;
}

bool flip_parse_uint(const char *text, uint32_t min, uint32_t max, uint32_t *value)
{
    if (*text == '\0') {
        return false;
    }
    uint32_t number = 0;
    for (; *text != '\0'; text++) {
        if (*text < '0' || *text > '9') {
            return false;
        }
        uint32_t digit = (uint32_t)(*text - '0');
        if (digit > max || number > (max - digit) / 10) {
            return false; // Above max, which also keeps the arithmetic from overflowing.
        }
        number = 10 * number + digit;
    }
    if (number < min) {
        return false;
    }
    *value = number;
    return true;
}
