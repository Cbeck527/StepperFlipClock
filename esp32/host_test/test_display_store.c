#include <string.h>

#include "display_store_format.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define HM(h, m) ((h) * 60 + (m))
#define COPAL 9 // Minutes per cycle.

static void assert_bytes(uint8_t b0, uint8_t b1, uint8_t b2, const uint8_t regs[3])
{
    const uint8_t expected[3] = {b0, b1, b2};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, regs, 3);
}

// Bytes worked out by hand from the layout in display_store_format.h.
static void test_encode_layout(void)
{
    uint8_t regs[3];
    display_store_encode(&(display_record_t){HM(12, 5), 4}, regs);
    assert_bytes(0x85, 0x92, 0x84, regs);
    display_store_encode(&(display_record_t){HM(23, 59), 8}, regs);
    assert_bytes(0xd9, 0xa3, 0x88, regs);
    display_store_encode(&(display_record_t){HM(0, 0), 0}, regs);
    assert_bytes(0x80, 0x80, 0x80, regs);
    display_store_encode(&(display_record_t){HM(9, 30), 14}, regs);
    assert_bytes(0xb0, 0x89, 0x8e, regs);
    TEST_ASSERT_EQUAL_HEX8(0xc4, display_store_changing_byte(4));
    TEST_ASSERT_EQUAL_HEX8(0xc0, display_store_changing_byte(0));
}

static display_store_status_t decode(uint8_t b0, uint8_t b1, uint8_t b2, uint32_t mpc, display_record_t *r)
{
    const uint8_t regs[3] = {b0, b1, b2};
    return display_store_decode(regs, mpc, r);
}

static void test_decode_this_firmware(void)
{
    display_record_t r;
    TEST_ASSERT_EQUAL_INT(DISPLAY_STORE_KNOWN, decode(0x85, 0x92, 0x84, COPAL, &r));
    TEST_ASSERT_EQUAL_INT(HM(12, 5), r.display);
    TEST_ASSERT_EQUAL_UINT32(4, r.cycle_minute);

    TEST_ASSERT_EQUAL_INT(DISPLAY_STORE_CHANGING, decode(0x85, 0x92, 0xc4, COPAL, &r));
    TEST_ASSERT_EQUAL_INT(HM(12, 5), r.display);
    TEST_ASSERT_EQUAL_UINT32(4, r.cycle_minute);

    // The cycle minute must fit the motor: 8 is the last for the Copal's 9, 14 for the GE's 15.
    TEST_ASSERT_EQUAL_INT(DISPLAY_STORE_KNOWN, decode(0x85, 0x92, 0x88, COPAL, &r));
    TEST_ASSERT_EQUAL_INT(DISPLAY_STORE_KNOWN, decode(0x85, 0x92, 0x8e, 15, &r));
    TEST_ASSERT_EQUAL_UINT32(14, r.cycle_minute);
}

// What the Arduino version left: setA2Time(1, hour, minute, 0, false, false, false).
static void test_decode_arduino_bytes(void)
{
    display_record_t r = {-7, 7};
    TEST_ASSERT_EQUAL_INT(DISPLAY_STORE_ARDUINO, decode(0x05, 0x12, 0x01, COPAL, &r));
    TEST_ASSERT_EQUAL_INT(HM(12, 5), r.display);
    TEST_ASSERT_EQUAL_UINT32(0, r.cycle_minute);
    TEST_ASSERT_EQUAL_INT(DISPLAY_STORE_ARDUINO, decode(0x59, 0x23, 0x01, COPAL, &r));
    TEST_ASSERT_EQUAL_INT(HM(23, 59), r.display);
}

// Each row must decode as INVALID and leave the record alone.
static void test_decode_rejects_everything_else(void)
{
    static const struct {
        uint8_t b[3];
        const char *why;
    } bad[] = {
        {{0x39, 0xa0, 0x01}, "Arduino after W9999: hour 100 as decToBcd(100) = 0xa0"},
        {{0x5a, 0x12, 0x01}, "Arduino minute not BCD"},
        {{0x60, 0x12, 0x01}, "Arduino minute 60"},
        {{0x05, 0x24, 0x01}, "Arduino hour 24"},
        {{0x05, 0x12, 0x02}, "Arduino layout with day 2"},
        {{0x05, 0x12, 0x00}, "Arduino layout with day 0"},
        {{0x00, 0x00, 0x00}, "all zero"},
        {{0xff, 0xff, 0xff}, "all ones"},
        {{0x85, 0x92, 0x89}, "cycle minute 9 for a 9-minute cycle"},
        {{0x85, 0x92, 0xbf}, "cycle minute 63"},
        {{0xe0, 0x92, 0x84}, "minute 60"},
        {{0x8a, 0x92, 0x84}, "minute not BCD"},
        {{0x85, 0xa4, 0x84}, "hour 24"},
        {{0x85, 0xc2, 0x84}, "12-hour bit set"},
        {{0x85, 0x8f, 0x84}, "hour not BCD"},
        {{0x85, 0x12, 0x84}, "mask bit missing on 0Ch"},
        {{0x05, 0x92, 0x84}, "mask bit missing on 0Bh"},
        {{0x85, 0x92, 0x04}, "mask bit missing on 0Dh"},
        {{0x05, 0x12, 0xc4}, "Arduino bytes with this firmware's changing mark on 0Dh"},
        {{0x05, 0x12, 0x81}, "Arduino bytes with a mask bit on 0Dh only"},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        display_record_t r = {-7, 7};
        TEST_ASSERT_EQUAL_INT_MESSAGE(DISPLAY_STORE_INVALID, display_store_decode(bad[i].b, COPAL, &r), bad[i].why);
        TEST_ASSERT_EQUAL_INT_MESSAGE(-7, r.display, bad[i].why);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(7, r.cycle_minute, bad[i].why);
    }
}

static void test_every_record_round_trips(void)
{
    for (int display = 0; display < 1440; display++) {
        for (uint32_t cm = 0; cm < 15; cm++) {
            uint8_t regs[3];
            display_store_encode(&(display_record_t){display, cm}, regs);
            display_record_t r;
            TEST_ASSERT_EQUAL_INT(DISPLAY_STORE_KNOWN, display_store_decode(regs, 15, &r));
            TEST_ASSERT_EQUAL_INT(display, r.display);
            TEST_ASSERT_EQUAL_UINT32(cm, r.cycle_minute);
            regs[2] = display_store_changing_byte(cm);
            TEST_ASSERT_EQUAL_INT(DISPLAY_STORE_CHANGING, display_store_decode(regs, 15, &r));
        }
    }
}

// Power-cut simulation. The DS3231 stores each byte of a write as it is acknowledged, so a
// cut can leave any prefix of a write. This replays the two writes the clock makes for each
// flip, the same way display_store.c does, and cuts power after every byte. After a cut, a
// KNOWN result must be exactly what the display shows, never a guess.

typedef struct {
    uint8_t regs[3];   // The DS3231 alarm 2 registers.
    int truth;         // What the display shows, -1 while it may be between two minutes,
                       // or NOT_CHECKED while a correction with w is being saved.
    uint32_t cycle;    // The cycle minute that goes with truth.
    int cuts;          // Cut points checked.
} sim_t;

#define NOT_CHECKED (-2)

static void check_cut(sim_t *s, const uint8_t regs[3])
{
    if (s->truth == NOT_CHECKED) {
        return;
    }
    display_record_t r = {-7, 7};
    display_store_status_t status = display_store_decode(regs, COPAL, &r);
    if (status == DISPLAY_STORE_KNOWN) {
        TEST_ASSERT_NOT_EQUAL_INT_MESSAGE(-1, s->truth, "KNOWN while the display was changing");
        TEST_ASSERT_EQUAL_INT_MESSAGE(s->truth, r.display, "KNOWN with the wrong display");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(s->cycle, r.cycle_minute, "KNOWN with the wrong cycle minute");
    }
    s->cuts++;
}

// Writes bytes into the registers one at a time, checking a cut before each and after the last.
static void write(sim_t *s, const uint8_t bytes[3])
{
    uint8_t regs[3];
    memcpy(regs, s->regs, 3);
    for (int i = 0; i < 3; i++) {
        check_cut(s, regs);
        regs[i] = bytes[i];
    }
    memcpy(s->regs, regs, 3);
}

static void begin(sim_t *s, uint32_t cycle_minute)
{
    const uint8_t bytes[3] = {s->regs[0], s->regs[1], display_store_changing_byte(cycle_minute)};
    write(s, bytes);
    check_cut(s, s->regs); // Cut after the mark, before the motor moves.
}

static void commit(sim_t *s, int display, uint32_t cycle_minute)
{
    uint8_t bytes[3];
    display_store_encode(&(display_record_t){display, cycle_minute}, bytes);
    write(s, bytes);
    s->truth = display;
    s->cycle = cycle_minute;
    check_cut(s, s->regs); // Cut while idle, with the record complete.
}

// One flip as clock_task.c does it: mark, move the motor, save.
static void flip(sim_t *s)
{
    int next = (s->truth + 1) % 1440;
    uint32_t next_cycle = (s->cycle + 1) % COPAL;
    begin(s, next_cycle);
    s->truth = -1; // From the first step, the display may be on either minute.
    check_cut(s, s->regs);
    commit(s, next, next_cycle);
}

static void test_power_cut_at_any_byte_never_gives_a_wrong_display(void)
{
    // Start from what the Arduino version left (12:05), then w 1258 as the owner would.
    sim_t s = {.regs = {0x05, 0x12, 0x01}, .truth = -1};
    display_record_t r;
    TEST_ASSERT_EQUAL_INT(DISPLAY_STORE_ARDUINO, display_store_decode(s.regs, COPAL, &r));
    s.truth = HM(12, 58); // The display really shows this; the stored 12:05 is stale.
    begin(&s, 0);
    commit(&s, HM(12, 58), 0);

    // Through an hour change, then through midnight, wrapping the cycle several times.
    for (int i = 0; i < 20; i++) {
        flip(&s);
    }
    TEST_ASSERT_EQUAL_INT(HM(13, 18), s.truth);
    // Someone trims the display to 23:50 with s, then w 2350. Until w is saved, the stored
    // 13:18 is wrong, as with any reading nobody has typed in yet, so this is not checked.
    s.truth = NOT_CHECKED;
    begin(&s, s.cycle);
    commit(&s, HM(23, 50), s.cycle);
    for (int i = 0; i < 20; i++) {
        flip(&s);
    }
    TEST_ASSERT_EQUAL_INT(HM(0, 10), s.truth);
    TEST_ASSERT_GREATER_THAN_INT(200, s.cuts);
}

// A cut in the middle of the first write over the Arduino bytes keeps them as they were.
static void test_cut_while_replacing_arduino_bytes_is_never_known(void)
{
    const uint8_t arduino[3] = {0x05, 0x12, 0x01};
    uint8_t mark[3] = {0x05, 0x12, display_store_changing_byte(0)};
    for (int k = 0; k <= 3; k++) {
        uint8_t regs[3];
        memcpy(regs, arduino, 3);
        memcpy(regs, mark, (size_t)k);
        display_record_t r;
        TEST_ASSERT_NOT_EQUAL_INT(DISPLAY_STORE_KNOWN, display_store_decode(regs, COPAL, &r));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_layout);
    RUN_TEST(test_decode_this_firmware);
    RUN_TEST(test_decode_arduino_bytes);
    RUN_TEST(test_decode_rejects_everything_else);
    RUN_TEST(test_every_record_round_trips);
    RUN_TEST(test_power_cut_at_any_byte_never_gives_a_wrong_display);
    RUN_TEST(test_cut_while_replacing_arduino_bytes_is_never_known);
    return UNITY_END();
}
