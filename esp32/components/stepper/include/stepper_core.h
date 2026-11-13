#pragma once

// The stepper's sequencing and step accounting in plain C. No ESP-IDF headers here:
// host_test/ builds this on the host. stepper.c connects it to the GPIOs and a timer.

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

// stepper_core_tick() returns this when no further tick is needed.
#define STEPPER_STOP (-1)

// What happened in one burst: from the first step after the motor was idle until it is idle again.
typedef struct {
    uint32_t steps;      // Steps taken.
    uint32_t min_gap_us; // Shortest time between two consecutive steps; 0 with fewer than 2 steps.
    uint32_t max_gap_us; // Longest time between two consecutive steps.
    uint32_t span_us;    // Time from the first step to the last.
} stepper_burst_t;

typedef struct {
    // Shared between stepper_core_add(), from any task, and stepper_core_tick().
    atomic_uint pending;  // Steps added that the tick has not taken over yet.
    atomic_bool running;  // A tick is scheduled or running and will look at pending again.

    // Owned by stepper_core_tick(), which only ever runs in one task.
    uint32_t period_us;
    bool reverse;
    uint32_t remaining;   // Steps taken over from pending and not taken yet.
    uint8_t phase;        // Position in the four-state coil sequence. Kept between bursts.
    int64_t first_step_us;
    int64_t last_step_us;
    stepper_burst_t burst; // The burst in progress.
    stepper_burst_t last_burst; // The last finished burst. Written before running is cleared.
} stepper_core_t;

// Starts idle with all coils off. reverse turns the motor the other way.
void stepper_core_init(stepper_core_t *core, uint32_t period_us, bool reverse);

// Adds steps to the current burst, or starts one. Safe to call from any task.
// Returns true when the caller must schedule a tick now, because the motor was idle.
bool stepper_core_add(stepper_core_t *core, uint32_t steps);

// True while steps are waiting or being taken, until the coils go off after the last one.
// Safe to call from any task.
bool stepper_core_busy(stepper_core_t *core);

// Takes the next step, or switches the coils off one period after the last step.
// Sets *coils to the coil pattern to output now: bit 0 is IN1 through bit 3 is IN4,
// and a set bit energises the coil. Returns the delay in microseconds before the next
// tick, which the caller must count from after it has output *coils, or STEPPER_STOP.
int32_t stepper_core_tick(stepper_core_t *core, int64_t now_us, uint8_t *coils);
