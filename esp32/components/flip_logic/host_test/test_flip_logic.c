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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_copal_227_bursts_over_two_cycles);
    RUN_TEST(test_ge_7_4305_bursts_over_a_day);
    return UNITY_END();
}
