#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <time.h>

#include "stepper_core.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define PERIOD 3000

// IN1 to IN4 as bits 0 to 3, written out from the sketch's set_pins(A, B, C, D) calls
// in step_to_state(), with A on IN1 through D on IN4.
#define PINS(a, b, c, d) ((a) | (b) << 1 | (c) << 2 | (d) << 3)
static const uint8_t STATE[4] = {PINS(0, 0, 1, 1), PINS(0, 1, 1, 0), PINS(1, 1, 0, 0), PINS(1, 0, 0, 1)};

static stepper_core_t core;

// Ticks once at now_us and checks that it took a step with the given coils.
static void expect_step(int64_t now_us, uint8_t coils)
{
    uint8_t out = 0xff;
    TEST_ASSERT_EQUAL_INT32(PERIOD, stepper_core_tick(&core, now_us, &out));
    TEST_ASSERT_EQUAL_HEX8(coils, out);
    TEST_ASSERT_TRUE(atomic_load(&core.running));
}

// Ticks once at now_us and checks that it switched the coils off and went idle.
static void expect_stop(int64_t now_us)
{
    uint8_t out = 0xff;
    TEST_ASSERT_EQUAL_INT32(STEPPER_STOP, stepper_core_tick(&core, now_us, &out));
    TEST_ASSERT_EQUAL_HEX8(0, out);
    TEST_ASSERT_FALSE(atomic_load(&core.running));
}

// The sketch starts in state 0 and moves to the next state before each step.
static void test_steps_follow_the_sketch_sequence(void)
{
    stepper_core_init(&core, PERIOD, false);
    TEST_ASSERT_TRUE(stepper_core_add(&core, 5));
    expect_step(0, STATE[1]);
    expect_step(3000, STATE[2]);
    expect_step(6000, STATE[3]);
    expect_step(9000, STATE[0]);
    expect_step(12000, STATE[1]);
    expect_stop(15000);
}

static void test_reverse_runs_the_sequence_backwards(void)
{
    stepper_core_init(&core, PERIOD, true);
    TEST_ASSERT_TRUE(stepper_core_add(&core, 4));
    expect_step(0, STATE[3]);
    expect_step(3000, STATE[2]);
    expect_step(6000, STATE[1]);
    expect_step(9000, STATE[0]);
    expect_stop(12000);
}

// The sketch never resets its position in the sequence, so a burst carries on from the last.
static void test_position_in_sequence_carries_over_between_bursts(void)
{
    stepper_core_init(&core, PERIOD, false);
    TEST_ASSERT_TRUE(stepper_core_add(&core, 2));
    expect_step(0, STATE[1]);
    expect_step(3000, STATE[2]);
    expect_stop(6000);
    TEST_ASSERT_TRUE(stepper_core_add(&core, 1));
    expect_step(100000, STATE[3]);
    expect_stop(103000);
}

// The sketch's add_steps() replaced a pending count; here steps added during a burst extend it.
static void test_adding_during_a_burst_extends_it(void)
{
    stepper_core_init(&core, PERIOD, false);
    TEST_ASSERT_TRUE(stepper_core_add(&core, 2));
    expect_step(0, STATE[1]);
    TEST_ASSERT_FALSE(stepper_core_add(&core, 3)); // Already running: no second timer.
    expect_step(3000, STATE[2]);
    TEST_ASSERT_FALSE(stepper_core_add(&core, 1));
    expect_step(6000, STATE[3]);
    expect_step(9000, STATE[0]);
    expect_step(12000, STATE[1]);
    expect_step(15000, STATE[2]);
    expect_stop(18000);
    TEST_ASSERT_EQUAL_UINT32(6, core.last_burst.steps);
}

static void test_zero_steps_leaves_the_motor_idle(void)
{
    stepper_core_init(&core, PERIOD, false);
    TEST_ASSERT_FALSE(stepper_core_add(&core, 0));
    TEST_ASSERT_FALSE(atomic_load(&core.running));
}

static void test_burst_reports_steps_and_gaps(void)
{
    stepper_core_init(&core, PERIOD, false);
    TEST_ASSERT_TRUE(stepper_core_add(&core, 4));
    expect_step(1000, STATE[1]);
    expect_step(4000, STATE[2]);  // gap 3000
    expect_step(7400, STATE[3]);  // gap 3400
    expect_step(10100, STATE[0]); // gap 2700
    TEST_ASSERT_EQUAL_UINT32(0, core.last_burst.steps); // Not reported until the burst ends.
    expect_stop(13100);
    TEST_ASSERT_EQUAL_UINT32(4, core.last_burst.steps);
    TEST_ASSERT_EQUAL_UINT32(2700, core.last_burst.min_gap_us);
    TEST_ASSERT_EQUAL_UINT32(3400, core.last_burst.max_gap_us);
    TEST_ASSERT_EQUAL_UINT32(9100, core.last_burst.span_us);

    // The next burst starts its figures afresh.
    TEST_ASSERT_TRUE(stepper_core_add(&core, 2));
    expect_step(50000, STATE[1]);
    expect_step(53100, STATE[2]);
    expect_stop(56100);
    TEST_ASSERT_EQUAL_UINT32(2, core.last_burst.steps);
    TEST_ASSERT_EQUAL_UINT32(3100, core.last_burst.min_gap_us);
    TEST_ASSERT_EQUAL_UINT32(3100, core.last_burst.max_gap_us);
    TEST_ASSERT_EQUAL_UINT32(3100, core.last_burst.span_us);
}

// The console task adds steps while the esp_timer task ticks. The risky moment is a step
// added just as the motor stops: whichever side sees it must schedule a tick, or the step
// waits forever and the motor reports busy for good. Here one thread plays the timer while
// another starts a one-step burst, waits a random moment, and adds a second step, which
// lands at every point of the first burst, including its last tick. Every step must be
// taken, the timer must never be started while already scheduled, and the motor must go idle.
#define ROUNDS 20000

static atomic_bool timer_scheduled;
static atomic_int double_starts;
static atomic_bool adder_done;
static uint32_t steps_taken;

static void start_timer_if_asked(bool asked)
{
    if (asked && atomic_exchange(&timer_scheduled, true)) {
        atomic_fetch_add(&double_starts, 1);
    }
}

// Waits up to a second for the motor to go idle.
static bool wait_idle(void)
{
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (stepper_core_busy(&core)) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - start.tv_sec > 1) {
            return false;
        }
        sched_yield();
    }
    return true;
}

static void *adder(void *arg)
{
    bool *stuck = arg;
    unsigned seed = 1;
    for (int i = 0; i < ROUNDS && !*stuck; i++) {
        *stuck = !wait_idle();
        start_timer_if_asked(stepper_core_add(&core, 1));
        for (volatile int spin = rand_r(&seed) % 3000; spin > 0; spin--) {
        }
        start_timer_if_asked(stepper_core_add(&core, 1));
    }
    *stuck = *stuck || !wait_idle();
    atomic_store(&adder_done, true);
    return NULL;
}

static void *timer(void *arg)
{
    (void)arg;
    while (!atomic_load(&adder_done) || atomic_load(&timer_scheduled)) {
        if (!atomic_exchange(&timer_scheduled, false)) {
            sched_yield();
            continue;
        }
        // A one-shot timer runs its callback, which may re-arm it; time does not matter here.
        uint8_t coils;
        int32_t delay;
        while ((delay = stepper_core_tick(&core, 0, &coils)) != STEPPER_STOP) {
            steps_taken += delay == PERIOD;
        }
    }
    return NULL;
}

static void test_steps_added_as_the_motor_stops_are_taken(void)
{
    stepper_core_init(&core, PERIOD, false);
    atomic_store(&timer_scheduled, false);
    atomic_store(&double_starts, 0);
    atomic_store(&adder_done, false);
    steps_taken = 0;
    bool stuck = false;

    pthread_t add_thread, timer_thread;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&timer_thread, NULL, timer, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&add_thread, NULL, adder, &stuck));
    pthread_join(add_thread, NULL);
    pthread_join(timer_thread, NULL);

    TEST_ASSERT_FALSE_MESSAGE(stuck, "the motor stayed busy with steps nobody would take");
    TEST_ASSERT_EQUAL_INT(0, atomic_load(&double_starts));
    TEST_ASSERT_EQUAL_UINT32(2 * ROUNDS, steps_taken);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_steps_follow_the_sketch_sequence);
    RUN_TEST(test_reverse_runs_the_sequence_backwards);
    RUN_TEST(test_position_in_sequence_carries_over_between_bursts);
    RUN_TEST(test_adding_during_a_burst_extends_it);
    RUN_TEST(test_zero_steps_leaves_the_motor_idle);
    RUN_TEST(test_burst_reports_steps_and_gaps);
    RUN_TEST(test_steps_added_as_the_motor_stops_are_taken);
    return UNITY_END();
}
