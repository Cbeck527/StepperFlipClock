#include "flip_logic.h"

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
