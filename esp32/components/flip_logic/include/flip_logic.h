#pragma once

// Flip clock rules in plain C. No ESP-IDF headers here: host_test/ builds this on the host.
// Nothing here reads a clock or drives a motor; the caller passes local time in and
// performs the steps these functions ask for.

#include <stdbool.h>
#include <stdint.h>

#define FLIP_MINUTES_PER_DAY 1440
#define FLIP_NO_TARGET (-1)

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

// What the display should do next, and why.
typedef enum {
    FLIP_IDLE,        // No target is set, so the motor stays still.
    FLIP_ON_TIME,     // The display shows the target.
    FLIP_ADVANCE,     // The display is behind the target: advance one minute.
    FLIP_WIND_AROUND, // The display is ahead by more than max_pause_minutes. A flip clock
                      // cannot go backwards, so advance one minute the long way round.
    FLIP_PAUSE,       // The display is ahead by at most max_pause_minutes: wait for the
                      // target to catch up. A DST fall-back looks like this.
} flip_action_t;

typedef struct {
    flip_action_t action;
    uint32_t steps; // Steps to take now; 0 unless action is FLIP_ADVANCE or FLIP_WIND_AROUND.
} flip_next_t;

typedef struct {
    flip_cycle_t cycle;
    uint32_t max_pause_minutes;
    int display;     // Minute of day the display shows, 0 to 1439.
    int target;      // Minute of day the display should show, or FLIP_NO_TARGET.
    bool advancing;  // The last minute's steps were handed out and are not counted yet.
} flip_clock_t;

// max_pause_minutes must be at most 720. display is the minute of day the display shows now.
void flip_clock_init(flip_clock_t *clock, uint32_t steps_per_cycle, uint32_t minutes_per_cycle,
                     uint32_t max_pause_minutes, int display);

// Sets the minute of day to drive the display towards, normally local time.
// FLIP_NO_TARGET stops it. Returns false, changing nothing, for any other value
// outside 0 to 1439.
bool flip_clock_set_target(flip_clock_t *clock, int minute_of_day);

// Records what the display actually shows, after someone checks it by eye.
// Returns false, changing nothing, outside 0 to 1439.
bool flip_clock_set_display(flip_clock_t *clock, int minute_of_day);

// Call whenever the motor is idle. Counts the minute whose steps the previous call
// handed out, then decides the next move. The display advances at most one minute
// per call, so a burst always matches exactly one flip.
flip_next_t flip_clock_update(flip_clock_t *clock);

// Parses exactly four digits HHMM, such as "2359", into a minute of day.
// Returns false for anything else, including hours above 23 and minutes above 59.
bool flip_parse_hhmm(const char *text, int *minute_of_day);

// Parses a decimal number of one or more digits, with no sign, spaces or other text,
// such as a console argument. Returns false, leaving *value alone, unless it is from min to max.
bool flip_parse_uint(const char *text, uint32_t min, uint32_t max, uint32_t *value);
