#include "stepper_core.h"

// Ported from step_to_state() in BurstClockSerialDS3231_copal227.ino, which drives pins
// A, B, C, D, wired to IN1 to IN4. Two coils are on in each state.
static const uint8_t COILS[4] = {
    0x0c, // (A, B, C, D) = (0, 0, 1, 1)
    0x06, // (0, 1, 1, 0)
    0x03, // (1, 1, 0, 0)
    0x09, // (1, 0, 0, 1)
};

void stepper_core_init(stepper_core_t *core, uint32_t period_us, bool reverse)
{
    *core = (stepper_core_t){
        .period_us = period_us,
        .reverse = reverse,
    };
    atomic_init(&core->pending, 0);
    atomic_init(&core->running, false);
}

bool stepper_core_add(stepper_core_t *core, uint32_t steps)
{
    if (steps == 0) {
        return false;
    }
    atomic_fetch_add(&core->pending, steps);
    // Only the caller that finds the motor idle starts it, so a tick is never scheduled twice.
    return !atomic_exchange(&core->running, true);
}

bool stepper_core_busy(stepper_core_t *core)
{
    // running is briefly false while a tick restarts for steps added as the motor stopped;
    // pending covers that moment.
    return atomic_load(&core->running) || atomic_load(&core->pending) != 0;
}

int32_t stepper_core_tick(stepper_core_t *core, int64_t now_us, uint8_t *coils)
{
    core->remaining += atomic_exchange(&core->pending, 0);
    if (core->remaining > 0) {
        core->remaining--;
        core->phase = (core->phase + (core->reverse ? 3 : 1)) % 4;
        *coils = COILS[core->phase];

        stepper_burst_t *burst = &core->burst;
        if (burst->steps == 0) {
            core->first_step_us = now_us;
        } else {
            uint32_t gap = (uint32_t)(now_us - core->last_step_us);
            if (burst->steps == 1 || gap < burst->min_gap_us) {
                burst->min_gap_us = gap;
            }
            if (gap > burst->max_gap_us) {
                burst->max_gap_us = gap;
            }
        }
        burst->steps++;
        burst->span_us = (uint32_t)(now_us - core->first_step_us);
        core->last_step_us = now_us;
        return (int32_t)core->period_us;
    }

    // One period after the last step: the coils go off and the motor is idle.
    *coils = 0;
    core->last_burst = core->burst;
    core->burst = (stepper_burst_t){0};
    atomic_store(&core->running, false);
    // Steps added after the exchange above, but before running was cleared, found the
    // motor running and did not schedule a tick, so this tick must.
    if (atomic_load(&core->pending) != 0 && !atomic_exchange(&core->running, true)) {
        return 0;
    }
    return STEPPER_STOP;
}
