#include "flip_logic.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

// Copal 227: 1280 steps per 9 minutes. The bursts are the differences of
// floor(k * 1280 / 9) for k = 1..9: 142, 284, 426, 568, 711, 853, 995, 1137, 1280.
static void test_copal_227_bursts_over_two_cycles(void)
{
    static const uint32_t expected[9] = {142, 142, 142, 142, 143, 142, 142, 142, 143};
    flip_cycle_t cycle;
    flip_cycle_init(&cycle, 1280, 9);

    uint32_t total = 0;
    for (int minute = 0; minute < 18; minute++) {
        uint32_t steps = flip_cycle_advance(&cycle);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected[minute % 9], steps, "burst size");
        total += steps;
    }
    TEST_ASSERT_EQUAL_UINT32(2560, total);
    TEST_ASSERT_EQUAL_UINT32(0, cycle.minute);
    TEST_ASSERT_EQUAL_UINT32(0, cycle.step);
}

// GE 7-4305: 2048 steps per 15 minutes, so bursts of 136 or 137 and no drift over a day.
static void test_ge_7_4305_bursts_over_a_day(void)
{
    flip_cycle_t cycle;
    flip_cycle_init(&cycle, 2048, 15);

    uint32_t total = 0;
    for (int minute = 0; minute < 1440; minute++) {
        uint32_t steps = flip_cycle_advance(&cycle);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(136, steps);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(137, steps);
        total += steps;
    }
    TEST_ASSERT_EQUAL_UINT32(96 * 2048, total); // 1440 minutes = 96 cycles of 15
}

// Restoring a saved cycle position gives the bursts the cycle would have given from there:
// minute 4 is followed by 143, 142, 142, 142, 143 (see the Copal sequence above), then 142.
static void test_cycle_set_minute_resumes_the_sequence(void)
{
    static const uint32_t expected[6] = {143, 142, 142, 142, 143, 142};
    flip_cycle_t cycle;
    flip_cycle_init(&cycle, 1280, 9);
    TEST_ASSERT_TRUE(flip_cycle_set_minute(&cycle, 4));
    TEST_ASSERT_EQUAL_UINT32(568, cycle.step); // floor(4 * 1280 / 9)
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_UINT32(expected[i], flip_cycle_advance(&cycle));
    }
    TEST_ASSERT_TRUE(flip_cycle_set_minute(&cycle, 8));
    TEST_ASSERT_EQUAL_UINT32(143, flip_cycle_advance(&cycle));
    TEST_ASSERT_EQUAL_UINT32(0, cycle.step);
    TEST_ASSERT_FALSE(flip_cycle_set_minute(&cycle, 9));
    TEST_ASSERT_EQUAL_UINT32(0, cycle.minute);
}

// The expected values below come from running the original sketch on the host
// (the hostsim harness) or are worked out by hand in the comments.

#define HM(h, m) ((h) * 60 + (m))

typedef struct {
    flip_action_t first; // The first decision.
    int flips;           // Minutes advanced.
    uint32_t steps;      // Steps handed out.
} run_t;

// Calls flip_clock_update() the way the clock task will, as each burst finishes,
// until the clock stops asking for steps.
static run_t settle(flip_clock_t *clock)
{
    run_t run = {0};
    for (int call = 0;; call++) {
        flip_next_t next = flip_clock_update(clock);
        if (call == 0) {
            run.first = next.action;
        }
        if (next.action != FLIP_ADVANCE && next.action != FLIP_WIND_AROUND) {
            return run;
        }
        TEST_ASSERT_LESS_THAN_INT_MESSAGE(FLIP_MINUTES_PER_DAY, run.flips, "never settled");
        run.flips++;
        run.steps += next.steps;
    }
}

static flip_clock_t copal_clock(int display)
{
    flip_clock_t clock;
    flip_clock_init(&clock, 1280, 9, 120, display);
    return clock;
}

// Sketch: 18 minutes of normal running issue 2560 steps, one flip per minute.
static void test_normal_running_flips_once_a_minute(void)
{
    flip_clock_t clock = copal_clock(HM(12, 0));
    flip_clock_set_target(&clock, HM(12, 0));
    TEST_ASSERT_EQUAL_INT(FLIP_ON_TIME, settle(&clock).first);

    uint32_t total = 0;
    for (int minute = 1; minute <= 18; minute++) {
        flip_clock_set_target(&clock, HM(12, 0) + minute);
        run_t run = settle(&clock);
        TEST_ASSERT_EQUAL_INT(FLIP_ADVANCE, run.first);
        TEST_ASSERT_EQUAL_INT(1, run.flips);
        total += run.steps;
    }
    TEST_ASSERT_EQUAL_UINT32(2560, total);
    TEST_ASSERT_EQUAL_INT(HM(12, 18), clock.display);
}

// The display belief changes when a burst finishes, not when it starts.
static void test_display_counts_a_minute_after_its_burst(void)
{
    flip_clock_t clock = copal_clock(HM(12, 0));
    flip_clock_set_target(&clock, HM(12, 1));

    flip_next_t next = flip_clock_update(&clock);
    TEST_ASSERT_EQUAL_INT(FLIP_ADVANCE, next.action);
    TEST_ASSERT_EQUAL_UINT32(142, next.steps);
    TEST_ASSERT_EQUAL_INT(HM(12, 0), clock.display);

    next = flip_clock_update(&clock);
    TEST_ASSERT_EQUAL_INT(FLIP_ON_TIME, next.action);
    TEST_ASSERT_EQUAL_UINT32(0, next.steps);
    TEST_ASSERT_EQUAL_INT(HM(12, 1), clock.display);
}

// Sketch: 23:58 -> 23:59 -> 00:00 is two ordinary flips, 284 steps.
static void test_midnight_wraps_to_zero(void)
{
    flip_clock_t clock = copal_clock(HM(23, 58));
    flip_clock_set_target(&clock, HM(23, 59));
    run_t first = settle(&clock);
    flip_clock_set_target(&clock, HM(0, 0));
    run_t second = settle(&clock);

    TEST_ASSERT_EQUAL_INT(FLIP_ADVANCE, second.first);
    TEST_ASSERT_EQUAL_INT(1, second.flips);
    TEST_ASSERT_EQUAL_UINT32(284, first.steps + second.steps);
    TEST_ASSERT_EQUAL_INT(0, clock.display);
}

// Sketch "dst" scenario: boot at 01:58, fall back at 02:00, then spring forward at 02:02.
static void test_dst_fall_back_pauses_and_spring_forward_winds(void)
{
    flip_clock_t clock = copal_clock(HM(1, 58));
    uint32_t steps = 0;
    flip_clock_set_target(&clock, HM(1, 59));
    steps += settle(&clock).steps;
    flip_clock_set_target(&clock, HM(2, 0));
    steps += settle(&clock).steps;
    TEST_ASSERT_EQUAL_UINT32(284, steps);

    // Local time repeats 01:00 to 01:59. The display holds 02:00 and the motor stays still.
    for (int minute = HM(1, 0); minute < HM(2, 0); minute++) {
        flip_clock_set_target(&clock, minute);
        run_t run = settle(&clock);
        TEST_ASSERT_EQUAL_INT(FLIP_PAUSE, run.first);
        TEST_ASSERT_EQUAL_UINT32(0, run.steps);
    }
    TEST_ASSERT_EQUAL_INT(HM(2, 0), clock.display);
    flip_clock_set_target(&clock, HM(2, 0));
    TEST_ASSERT_EQUAL_INT(FLIP_ON_TIME, settle(&clock).first);

    // Normal flips resume.
    flip_clock_set_target(&clock, HM(2, 1));
    steps += settle(&clock).steps;
    flip_clock_set_target(&clock, HM(2, 2));
    steps += settle(&clock).steps;
    TEST_ASSERT_EQUAL_UINT32(568, steps);

    // Local time jumps from 02:02 to 03:03: 61 flips in one run. The cycle is at minute 4,
    // so that is floor(65 * 1280 / 9) - floor(4 * 1280 / 9) = 9244 - 568 = 8676 steps.
    flip_clock_set_target(&clock, HM(3, 3));
    run_t run = settle(&clock);
    TEST_ASSERT_EQUAL_INT(FLIP_ADVANCE, run.first);
    TEST_ASSERT_EQUAL_INT(61, run.flips);
    TEST_ASSERT_EQUAL_UINT32(8676, run.steps);
    TEST_ASSERT_EQUAL_INT(HM(3, 3), clock.display);
}

// Sketch "catchup" scenario: display shows 14:30 at 12:00, beyond the 120-minute pause,
// so it winds forward the long way. In 20 minutes the sketch reaches 12:20 after
// 186,311 steps: 1310 flips, floor(1310 * 1280 / 9).
static void test_display_far_ahead_winds_around(void)
{
    flip_clock_t clock = copal_clock(HM(14, 30));
    flip_clock_set_target(&clock, HM(12, 20));
    run_t run = settle(&clock);
    TEST_ASSERT_EQUAL_INT(FLIP_WIND_AROUND, run.first);
    TEST_ASSERT_EQUAL_INT(1310, run.flips);
    TEST_ASSERT_EQUAL_UINT32(186311, run.steps);
    TEST_ASSERT_EQUAL_INT(HM(12, 20), clock.display);
}

static flip_action_t first_action(uint32_t max_pause, int display, int target)
{
    flip_clock_t clock;
    flip_clock_init(&clock, 1280, 9, max_pause, display);
    flip_clock_set_target(&clock, target);
    return flip_clock_update(&clock).action;
}

// Sketch "ahead119/120/121" scenarios at 12:00: 119 and 120 minutes ahead pause, 121 winds.
static void test_pause_threshold_both_sides(void)
{
    TEST_ASSERT_EQUAL_INT(FLIP_PAUSE, first_action(120, HM(12, 0) + 119, HM(12, 0)));
    TEST_ASSERT_EQUAL_INT(FLIP_PAUSE, first_action(120, HM(12, 0) + 120, HM(12, 0)));
    TEST_ASSERT_EQUAL_INT(FLIP_WIND_AROUND, first_action(120, HM(12, 0) + 121, HM(12, 0)));
    // The same boundary across midnight: 00:30 shown at 22:30 is exactly 120 ahead.
    TEST_ASSERT_EQUAL_INT(FLIP_PAUSE, first_action(120, HM(0, 30), HM(22, 30)));
    TEST_ASSERT_EQUAL_INT(FLIP_WIND_AROUND, first_action(120, HM(0, 31), HM(22, 30)));
}

static void test_pause_threshold_is_configurable(void)
{
    TEST_ASSERT_EQUAL_INT(FLIP_PAUSE, first_action(30, HM(12, 30), HM(12, 0)));
    TEST_ASSERT_EQUAL_INT(FLIP_WIND_AROUND, first_action(30, HM(12, 31), HM(12, 0)));
    // With no pause at all, a fall-back winds forward 23 hours.
    TEST_ASSERT_EQUAL_INT(FLIP_WIND_AROUND, first_action(0, HM(2, 0), HM(1, 0)));
}

// The reason splits at half a day: up to 720 minutes behind is advancing, more is winding round.
static void test_advance_and_wind_around_split_at_half_a_day(void)
{
    TEST_ASSERT_EQUAL_INT(FLIP_ADVANCE, first_action(120, HM(0, 0), HM(12, 0)));
    TEST_ASSERT_EQUAL_INT(FLIP_WIND_AROUND, first_action(120, HM(0, 0), HM(12, 1)));
}

static void test_no_target_keeps_still(void)
{
    flip_clock_t clock = copal_clock(HM(12, 0));
    TEST_ASSERT_EQUAL_INT(FLIP_IDLE, settle(&clock).first);
    flip_clock_set_target(&clock, HM(13, 0));
    TEST_ASSERT_TRUE(flip_clock_set_target(&clock, FLIP_NO_TARGET));
    run_t run = settle(&clock);
    TEST_ASSERT_EQUAL_INT(FLIP_IDLE, run.first);
    TEST_ASSERT_EQUAL_UINT32(0, run.steps);
    TEST_ASSERT_EQUAL_INT(HM(12, 0), clock.display);
}

// Sketch: "D9999" made the target minute 6039 and parked the display at 04:40;
// "W9999" stored hour 100. Out-of-range minutes are now refused and change nothing.
static void test_out_of_range_minutes_are_rejected(void)
{
    flip_clock_t clock = copal_clock(HM(12, 0));
    TEST_ASSERT_TRUE(flip_clock_set_target(&clock, HM(12, 0)));
    TEST_ASSERT_FALSE(flip_clock_set_target(&clock, 6039));
    TEST_ASSERT_FALSE(flip_clock_set_target(&clock, FLIP_MINUTES_PER_DAY));
    TEST_ASSERT_FALSE(flip_clock_set_target(&clock, -2));
    TEST_ASSERT_EQUAL_INT(HM(12, 0), clock.target);

    TEST_ASSERT_FALSE(flip_clock_set_display(&clock, 6039));
    TEST_ASSERT_FALSE(flip_clock_set_display(&clock, FLIP_MINUTES_PER_DAY));
    TEST_ASSERT_FALSE(flip_clock_set_display(&clock, -1));
    TEST_ASSERT_EQUAL_INT(HM(12, 0), clock.display);
    TEST_ASSERT_TRUE(flip_clock_set_display(&clock, HM(23, 59)));
    TEST_ASSERT_EQUAL_INT(HM(23, 59), clock.display);
}

static void test_parse_uint_accepts_the_range(void)
{
    uint32_t value = 7;
    TEST_ASSERT_TRUE(flip_parse_uint("1", 1, 1280, &value));
    TEST_ASSERT_EQUAL_UINT32(1, value);
    TEST_ASSERT_TRUE(flip_parse_uint("1280", 1, 1280, &value));
    TEST_ASSERT_EQUAL_UINT32(1280, value);
    TEST_ASSERT_TRUE(flip_parse_uint("0143", 1, 1280, &value));
    TEST_ASSERT_EQUAL_UINT32(143, value);
    TEST_ASSERT_TRUE(flip_parse_uint("0", 0, 255, &value));
    TEST_ASSERT_EQUAL_UINT32(0, value);
    TEST_ASSERT_TRUE(flip_parse_uint("4294967295", 0, UINT32_MAX, &value));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, value);
}

static void test_parse_uint_rejects_everything_else(void)
{
    // For s, which takes 1 to 1280 steps. "-5" is the sketch's runaway S-5.
    static const char *const bad[] = {
        "-5", "0", "1281", "9999", "+5", "", " 5", "5 ", "5\r", "12a", "0x10", "1e3",
        "4294967296", "99999999999999999999",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        uint32_t value = 7;
        TEST_ASSERT_FALSE_MESSAGE(flip_parse_uint(bad[i], 1, 1280, &value), bad[i]);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(7, value, bad[i]);
    }
    // A single digit above a small max, and the first number past UINT32_MAX.
    uint32_t value = 7;
    TEST_ASSERT_FALSE(flip_parse_uint("7", 0, 5, &value));
    TEST_ASSERT_FALSE(flip_parse_uint("", 0, 255, &value));
    TEST_ASSERT_FALSE(flip_parse_uint("4294967296", 0, UINT32_MAX, &value));
    TEST_ASSERT_EQUAL_UINT32(7, value);
}

static void test_parse_hhmm(void)
{
    int minute = -7;
    TEST_ASSERT_TRUE(flip_parse_hhmm("0000", &minute));
    TEST_ASSERT_EQUAL_INT(0, minute);
    TEST_ASSERT_TRUE(flip_parse_hhmm("2359", &minute));
    TEST_ASSERT_EQUAL_INT(1439, minute);
    TEST_ASSERT_TRUE(flip_parse_hhmm("1234", &minute));
    TEST_ASSERT_EQUAL_INT(754, minute);

    static const char *const bad[] = {
        "9999", "2400", "1260", "123", "12345", "12a4", "", "-123", " 123", "1234\r", "12:3",
        // The characters just outside '0'-'9', placed where they would give an in-range time.
        "1:00", "0/00",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        minute = -7;
        TEST_ASSERT_FALSE_MESSAGE(flip_parse_hhmm(bad[i], &minute), bad[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(-7, minute, bad[i]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_copal_227_bursts_over_two_cycles);
    RUN_TEST(test_ge_7_4305_bursts_over_a_day);
    RUN_TEST(test_cycle_set_minute_resumes_the_sequence);
    RUN_TEST(test_normal_running_flips_once_a_minute);
    RUN_TEST(test_display_counts_a_minute_after_its_burst);
    RUN_TEST(test_midnight_wraps_to_zero);
    RUN_TEST(test_dst_fall_back_pauses_and_spring_forward_winds);
    RUN_TEST(test_display_far_ahead_winds_around);
    RUN_TEST(test_pause_threshold_both_sides);
    RUN_TEST(test_pause_threshold_is_configurable);
    RUN_TEST(test_advance_and_wind_around_split_at_half_a_day);
    RUN_TEST(test_no_target_keeps_still);
    RUN_TEST(test_out_of_range_minutes_are_rejected);
    RUN_TEST(test_parse_hhmm);
    RUN_TEST(test_parse_uint_accepts_the_range);
    RUN_TEST(test_parse_uint_rejects_everything_else);
    return UNITY_END();
}
