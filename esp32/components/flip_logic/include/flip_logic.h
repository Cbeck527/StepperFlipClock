#pragma once

// Flip clock rules in plain C. No ESP-IDF headers here: host_test/ builds this on the host.

#include <stdint.h>

// Where the motor is within one turn of the flip mechanism.
// A turn advances the display by minutes_per_cycle minutes in steps_per_cycle steps,
// which rarely divide evenly, so each minute's burst is rounded down from the
// cycle's start and the remainder carries to later minutes.
typedef struct {
    uint32_t steps_per_cycle;
    uint32_t minutes_per_cycle;
    uint32_t minute; // Minutes advanced in this cycle, 0 to minutes_per_cycle - 1.
    uint32_t step;   // Steps taken in this cycle, 0 to steps_per_cycle - 1.
} flip_cycle_t;

// Starts at the beginning of a cycle. Both counts must be at least 1.
void flip_cycle_init(flip_cycle_t *cycle, uint32_t steps_per_cycle, uint32_t minutes_per_cycle);

// Advances one minute and returns how many steps the motor must take for it.
// For the Copal 227 (1280 steps per 9 minutes) that is 142 or 143.
uint32_t flip_cycle_advance(flip_cycle_t *cycle);
