// Runs clock_task.c's passes on the host against a fake DS3231 alarm-2 store that can fail
// writes, a stepper that finishes each burst at once, and the real flip_logic.

#include <stdlib.h>
#include <string.h>
#include <time.h>

// A fixed local time, 12:00 UTC, so a minute change during the test cannot move the target.
static time_t fake_time(time_t *t)
{
    (void)t;
    return (time_t)20000 * 86400 + 12 * 3600;
}
#define time(t) fake_time(t)

#include "unity.h"

// The task's state and pass() are static, so the test builds the source itself.
#include "../components/clock_task/clock_task.c"

int stub_log_errors;

#define COPAL_STEPS 1280
#define COPAL_MINUTES 9

static uint32_t s_moves; // stepper_add() calls.
static uint8_t s_regs[DISPLAY_STORE_LEN];
static esp_err_t s_write_err; // What the next writes to the store return.

void stepper_add(uint32_t steps)
{
    (void)steps;
    s_moves++;
}
bool stepper_is_busy(void) { return false; }
static bool s_trusted;
bool clock_time_trusted(void) { return s_trusted; }

esp_err_t display_store_load(uint32_t minutes_per_cycle, uint8_t regs[DISPLAY_STORE_LEN],
                             display_store_status_t *status, display_record_t *record)
{
    memcpy(regs, s_regs, DISPLAY_STORE_LEN);
    *status = display_store_decode(s_regs, minutes_per_cycle, record);
    return ESP_OK;
}

esp_err_t display_store_begin(uint32_t cycle_minute)
{
    if (s_write_err == ESP_OK) {
        s_regs[2] = display_store_changing_byte(cycle_minute);
    }
    return s_write_err;
}

esp_err_t display_store_commit(const display_record_t *record)
{
    if (s_write_err == ESP_OK) {
        display_store_encode(record, s_regs);
    }
    return s_write_err;
}

static int now_minute(void)
{
    return 12 * 60;
}

// What a boot would make of the store right now: KNOWN must never name a minute the display
// does not show.
static void assert_store_safe(void)
{
    display_record_t r;
    if (display_store_decode(s_regs, COPAL_MINUTES, &r) == DISPLAY_STORE_KNOWN) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(s_clock.display, r.display, "KNOWN record of the wrong minute");
    }
}

void setUp(void)
{
    s_moves = 0;
    s_trusted = true;
    s_write_err = ESP_OK;
    stub_log_errors = 0;
    // A record saved five minutes ago, so the clock wants to catch up.
    display_record_t saved = {(now_minute() + FLIP_MINUTES_PER_DAY - 5) % FLIP_MINUTES_PER_DAY, 0};
    display_store_encode(&saved, s_regs);
    const clock_task_config_t config = {COPAL_STEPS, COPAL_MINUTES, 120};
    TEST_ASSERT_EQUAL_INT(ESP_OK, clock_task_start(&config));
    TEST_ASSERT_TRUE(s_known);
}
void tearDown(void) {}

static void test_failed_changing_mark_keeps_the_motor_still(void)
{
    const int start = s_clock.display;
    s_write_err = ESP_FAIL;
    for (int i = 0; i < 100; i++) { // One second of passes.
        pass();
        assert_store_safe();
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s_moves, "the motor moved without the changing mark");
    TEST_ASSERT_EQUAL_INT(start, s_clock.display);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, stub_log_errors, "the failure should be logged once");
    char text[128];
    clock_task_describe(text, sizeof text);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(text, "stopped"), text);

    // The RTC answers again: the next pass moves, and the clock catches up.
    s_write_err = ESP_OK;
    for (int i = 0; i < 20; i++) {
        pass();
        assert_store_safe();
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(5, s_moves);
    TEST_ASSERT_EQUAL_INT(now_minute(), s_clock.display);
    display_record_t r;
    TEST_ASSERT_EQUAL_INT(DISPLAY_STORE_KNOWN, display_store_decode(s_regs, COPAL_MINUTES, &r));
    TEST_ASSERT_EQUAL_INT(now_minute(), r.display);
    TEST_ASSERT_EQUAL_UINT32(s_clock.cycle.minute, r.cycle_minute);
    TEST_ASSERT_EQUAL_INT(1, stub_log_errors);
}

// The RTC fails after a burst has started: the commit fails, so the record must stay
// "changing", and nothing more moves.
static void test_failure_mid_catch_up_never_leaves_a_wrong_known_record(void)
{
    pass(); // Mark written, first burst handed out.
    TEST_ASSERT_EQUAL_UINT32(1, s_moves);
    s_write_err = ESP_FAIL;
    for (int i = 0; i < 100; i++) {
        pass();
        assert_store_safe();
    }
    TEST_ASSERT_EQUAL_UINT32(1, s_moves);
    display_record_t r;
    TEST_ASSERT_EQUAL_INT(DISPLAY_STORE_CHANGING, display_store_decode(s_regs, COPAL_MINUTES, &r));
    TEST_ASSERT_EQUAL_INT(1, stub_log_errors);
}

static void assert_now(const char *expected)
{
    char text[160];
    clock_task_describe(text, sizeof text);
    TEST_ASSERT_EQUAL_STRING(expected, text);
}

static void passes(int count)
{
    for (int i = 0; i < count; i++) {
        pass();
    }
}

// What w and now print must hold when they print it. Local time is 12:00 throughout, and each
// pass finishes a burst, as the motor would between two passes.
static void test_now_is_true_at_every_moment(void)
{
    // The owner's bench: the last pass before w saw no usable target.
    s_trusted = false;
    pass();
    s_trusted = true;

    TEST_ASSERT_EQUAL_INT(ESP_OK, clock_task_set_display(6 * 60 + 45));
    assert_now("Display 06:45 is 315 minutes behind 12:00: catching up");
    pass(); // The first flip is under way.
    assert_now("Display 06:46 is 314 minutes behind 12:00: catching up");
    passes(299);
    assert_now("Display 11:45 is 15 minutes behind 12:00: catching up");
    passes(15);
    assert_now("Display 12:00, following local time");
    passes(3);
    assert_now("Display 12:00, following local time");
    TEST_ASSERT_EQUAL_UINT32(315, s_moves);

    clock_task_set_display(11 * 60 + 59); // Each minute's normal flip.
    assert_now("Display 11:59, following local time");

    clock_task_set_display(12 * 60 + 30);
    assert_now("Display 12:30 is 30 minutes ahead of 12:00: waiting for the time to catch up");
    passes(3);
    assert_now("Display 12:30 is 30 minutes ahead of 12:00: waiting for the time to catch up");

    clock_task_set_display(14 * 60 + 30);
    assert_now("Display 14:30 is 150 minutes ahead of 12:00, more than the 120-minute pause: "
               "winding forward 1290 minutes");
    pass();
    assert_now("Display 14:31 is 151 minutes ahead of 12:00, more than the 120-minute pause: "
               "winding forward 1289 minutes");

    s_trusted = false;
    assert_now("Display 14:31, not moving: the time is not trusted");
}

#define HM(h, m) ((h) * 60 + (m))

// d moves the display to a fixed time and keeps it there, t 0 stops it, t 1 and every
// restart go back to local time. Local time is 12:00; setUp leaves the display at 11:55.
static void test_d_holds_t_stops_and_a_restart_follows(void)
{
    passes(6);
    assert_now("Display 12:00, following local time");
    TEST_ASSERT_EQUAL_UINT32(5, s_moves);

    // 15 minutes behind the display: local time would wait, a held time goes the long way.
    clock_task_hold(HM(11, 45));
    assert_now("Display 12:00 is moving forward 1425 minutes to 11:45, set with d");
    pass();
    assert_now("Display 12:01 is moving forward 1424 minutes to 11:45, set with d");
    passes(1424);
    assert_now("Display 11:45, held there by d. t 1 follows local time again");
    passes(5);
    TEST_ASSERT_EQUAL_UINT32(5 + 1425, s_moves);

    // A held time does not depend on the time of day, so it moves without trusted time.
    s_trusted = false;
    clock_task_hold(HM(11, 50));
    passes(6);
    assert_now("Display 11:50, held there by d. t 1 follows local time again");
    TEST_ASSERT_EQUAL_UINT32(5 + 1425 + 5, s_moves);
    s_trusted = true;

    clock_task_follow(false);
    assert_now("Display 11:50, stopped with t 0. t 1 follows local time again");
    passes(5);
    TEST_ASSERT_EQUAL_UINT32(1435, s_moves);

    clock_task_follow(true);
    assert_now("Display 11:50 is 10 minutes behind 12:00: catching up");
    passes(11);
    assert_now("Display 12:00, following local time");
    TEST_ASSERT_EQUAL_UINT32(1445, s_moves);

    // Nothing about d or t is saved: a restart follows local time.
    clock_task_hold(HM(13, 0));
    const clock_task_config_t config = {COPAL_STEPS, COPAL_MINUTES, 120};
    TEST_ASSERT_EQUAL_INT(ESP_OK, clock_task_start(&config));
    assert_now("Display 12:00, following local time");
    passes(3);
    TEST_ASSERT_EQUAL_UINT32(1445, s_moves);
}

// d does not move a display nobody has read, and w keeps the held time.
static void test_d_waits_for_w(void)
{
    memset(s_regs, 0, sizeof s_regs);
    const clock_task_config_t config = {COPAL_STEPS, COPAL_MINUTES, 120};
    TEST_ASSERT_EQUAL_INT(ESP_OK, clock_task_start(&config));
    clock_task_hold(HM(12, 30));
    passes(5);
    TEST_ASSERT_EQUAL_UINT32(0, s_moves);
    assert_now("Display unknown: nothing usable is saved. Set it with: w HHMM");

    clock_task_set_display(HM(12, 0));
    assert_now("Display 12:00 is moving forward 30 minutes to 12:30, set with d");
    passes(31);
    TEST_ASSERT_EQUAL_UINT32(30, s_moves);
    assert_now("Display 12:30, held there by d. t 1 follows local time again");
}

int main(void)
{
    setenv("TZ", "UTC0", 1);
    tzset();
    UNITY_BEGIN();
    RUN_TEST(test_failed_changing_mark_keeps_the_motor_still);
    RUN_TEST(test_failure_mid_catch_up_never_leaves_a_wrong_known_record);
    RUN_TEST(test_now_is_true_at_every_moment);
    RUN_TEST(test_d_holds_t_stops_and_a_restart_follows);
    RUN_TEST(test_d_waits_for_w);
    return UNITY_END();
}
