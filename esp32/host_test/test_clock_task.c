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
bool clock_time_trusted(void) { return true; }

esp_err_t display_store_load(uint32_t minutes_per_cycle, display_store_status_t *status,
                             display_record_t *record)
{
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

int main(void)
{
    setenv("TZ", "UTC0", 1);
    tzset();
    UNITY_BEGIN();
    RUN_TEST(test_failed_changing_mark_keeps_the_motor_still);
    RUN_TEST(test_failure_mid_catch_up_never_leaves_a_wrong_known_record);
    return UNITY_END();
}
