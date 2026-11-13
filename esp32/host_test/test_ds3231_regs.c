#define _DEFAULT_SOURCE // timegm() and gmtime_r() from the host C library, the oracle for dates.

#include <string.h>
#include <time.h>

#include "ds3231_regs.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define DT(y, mo, d, h, mi, s) ((ds3231_datetime_t){y, mo, d, h, mi, s})

static void assert_datetime(ds3231_datetime_t expected, ds3231_datetime_t actual)
{
    TEST_ASSERT_EQUAL_INT_ARRAY(&expected.year, &actual.year, 6);
}

// Register bytes worked out by hand from Figure 1. 2026-11-13 was a Friday: day 6 with 1 as Sunday.
static void test_encode_matches_the_datasheet_layout(void)
{
    uint8_t regs[DS3231_TIME_LEN];
    ds3231_encode_time(&DT(2026, 11, 13, 17, 45, 9), regs);
    const uint8_t expected[DS3231_TIME_LEN] = {0x09, 0x45, 0x17, 0x06, 0x13, 0x11, 0x26};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, regs, DS3231_TIME_LEN);

    // The BCD tens digits at their largest, 24-hour mode with the 20-hour bit, and a Sunday.
    ds3231_encode_time(&DT(2099, 12, 27, 23, 59, 59), regs);
    const uint8_t late[DS3231_TIME_LEN] = {0x59, 0x59, 0x23, 0x01, 0x27, 0x12, 0x99};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(late, regs, DS3231_TIME_LEN);

    // 2000-01-01 was a Saturday.
    ds3231_encode_time(&DT(2000, 1, 1, 0, 0, 0), regs);
    const uint8_t early[DS3231_TIME_LEN] = {0x00, 0x00, 0x00, 0x07, 0x01, 0x01, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(early, regs, DS3231_TIME_LEN);
}

static void test_decode_reads_the_datasheet_layout(void)
{
    const uint8_t regs[DS3231_TIME_LEN] = {0x09, 0x45, 0x17, 0x06, 0x13, 0x11, 0x26};
    ds3231_datetime_t t;
    TEST_ASSERT_TRUE(ds3231_decode_time(regs, &t));
    assert_datetime(DT(2026, 11, 13, 17, 45, 9), t);
}

// Hours register in 12-hour mode: bit 6 set, bit 5 PM, bit 4 the 10-hour digit.
static int decode_hour(uint8_t hours_reg)
{
    uint8_t regs[DS3231_TIME_LEN] = {0x00, 0x00, hours_reg, 0x06, 0x13, 0x11, 0x26};
    ds3231_datetime_t t = DT(0, 0, 0, -1, 0, 0);
    return ds3231_decode_time(regs, &t) ? t.hour : -1;
}

static void test_decode_12_hour_mode(void)
{
    TEST_ASSERT_EQUAL_INT(0, decode_hour(0x40 | 0x12));         // 12 AM is midnight.
    TEST_ASSERT_EQUAL_INT(1, decode_hour(0x40 | 0x01));         // 1 AM
    TEST_ASSERT_EQUAL_INT(11, decode_hour(0x40 | 0x11));        // 11 AM
    TEST_ASSERT_EQUAL_INT(12, decode_hour(0x40 | 0x20 | 0x12)); // 12 PM is noon.
    TEST_ASSERT_EQUAL_INT(13, decode_hour(0x40 | 0x20 | 0x01)); // 1 PM
    TEST_ASSERT_EQUAL_INT(23, decode_hour(0x40 | 0x20 | 0x11)); // 11 PM
    TEST_ASSERT_EQUAL_INT(-1, decode_hour(0x40 | 0x00));        // There is no hour 0 in 12-hour mode.
    TEST_ASSERT_EQUAL_INT(-1, decode_hour(0x40 | 0x13));
    TEST_ASSERT_EQUAL_INT(-1, decode_hour(0x40 | 0x20 | 0x13));
}

static void test_decode_24_hour_mode(void)
{
    TEST_ASSERT_EQUAL_INT(0, decode_hour(0x00));
    TEST_ASSERT_EQUAL_INT(12, decode_hour(0x12));
    TEST_ASSERT_EQUAL_INT(20, decode_hour(0x20)); // Bit 5 is the 20-hour digit here, not PM.
    TEST_ASSERT_EQUAL_INT(23, decode_hour(0x23));
    TEST_ASSERT_EQUAL_INT(-1, decode_hour(0x24));
    TEST_ASSERT_EQUAL_INT(-1, decode_hour(0x1a)); // Not BCD.
    TEST_ASSERT_EQUAL_INT(-1, decode_hour(0x80)); // Bit 7 always reads 0.
}

// Each row is a valid time with one register changed to something the DS3231 cannot hold.
static void test_decode_rejects_impossible_registers(void)
{
    static const struct {
        int reg;
        uint8_t value;
        const char *why;
    } bad[] = {
        {0, 0x5a, "seconds not BCD"},
        {0, 0x60, "second 60"},
        {0, 0x80, "seconds bit 7"},
        {1, 0x60, "minute 60"},
        {1, 0x0f, "minutes not BCD"},
        {3, 0x00, "day of week 0"},
        {3, 0x08, "day of week 8"},
        {4, 0x00, "date 0"},
        {4, 0x32, "date 32"},
        {4, 0x31, "31 November"},
        {4, 0x1a, "date not BCD"},
        {5, 0x00, "month 0"},
        {5, 0x13, "month 13"},
        {5, 0x91, "century bit: year 2126"},
        {5, 0x51, "month bits 6-5"},
        {6, 0xa0, "year 2100"},
        {6, 0x2f, "year not BCD"},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        uint8_t regs[DS3231_TIME_LEN] = {0x09, 0x45, 0x17, 0x06, 0x13, 0x11, 0x26};
        regs[bad[i].reg] = bad[i].value;
        ds3231_datetime_t t = DT(1, 1, 1, 1, 1, 1);
        TEST_ASSERT_FALSE_MESSAGE(ds3231_decode_time(regs, &t), bad[i].why);
        assert_datetime(DT(1, 1, 1, 1, 1, 1), t);
    }
}

static void test_decode_29_february_only_in_leap_years(void)
{
    ds3231_datetime_t t;
    const uint8_t leap[DS3231_TIME_LEN] = {0, 0, 0, 0x03, 0x29, 0x02, 0x28}; // 2028
    TEST_ASSERT_TRUE(ds3231_decode_time(leap, &t));
    const uint8_t y2000[DS3231_TIME_LEN] = {0, 0, 0, 0x03, 0x29, 0x02, 0x00}; // 2000 is a leap year.
    TEST_ASSERT_TRUE(ds3231_decode_time(y2000, &t));
    const uint8_t common[DS3231_TIME_LEN] = {0, 0, 0, 0x03, 0x29, 0x02, 0x27}; // 2027
    TEST_ASSERT_FALSE(ds3231_decode_time(common, &t));
}

// What the DS3231 holds after its first power-up: 01/01/00 01 00:00:00 (section Power Control).
static void test_decode_power_on_reset_value(void)
{
    const uint8_t regs[DS3231_TIME_LEN] = {0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00};
    ds3231_datetime_t t;
    TEST_ASSERT_TRUE(ds3231_decode_time(regs, &t));
    assert_datetime(DT(2000, 1, 1, 0, 0, 0), t);
}

// Every day from 2000 to 2099 at a time whose fields all differ: encode, decode, and both
// Unix conversions agree with each other and with the host C library.
static void test_every_day_round_trips_and_matches_the_host(void)
{
    int days = 0;
    for (time_t day = 946684800; day < 4102444800; day += 86400, days++) { // 2000-01-01 to 2100-01-01
        time_t when = day + 13 * 3600 + 47 * 60 + 29;
        struct tm g;
        gmtime_r(&when, &g);
        ds3231_datetime_t t = DT(g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec);

        TEST_ASSERT_EQUAL_INT64(when, ds3231_datetime_to_unix(&t));
        ds3231_datetime_t back;
        ds3231_datetime_from_unix(when, &back);
        assert_datetime(t, back);

        uint8_t regs[DS3231_TIME_LEN];
        ds3231_encode_time(&t, regs);
        TEST_ASSERT_EQUAL_UINT8(g.tm_wday + 1, regs[3]);
        ds3231_datetime_t decoded;
        TEST_ASSERT_TRUE(ds3231_decode_time(regs, &decoded));
        assert_datetime(t, decoded);
    }
    TEST_ASSERT_EQUAL_INT(36525, days);
}

static void test_parse_accepts_real_dates(void)
{
    ds3231_datetime_t t;
    TEST_ASSERT_TRUE(ds3231_parse_datetime("20261113174509", &t));
    assert_datetime(DT(2026, 11, 13, 17, 45, 9), t);
    TEST_ASSERT_TRUE(ds3231_parse_datetime("20000101000000", &t));
    assert_datetime(DT(2000, 1, 1, 0, 0, 0), t);
    TEST_ASSERT_TRUE(ds3231_parse_datetime("20991231235959", &t));
    assert_datetime(DT(2099, 12, 31, 23, 59, 59), t);
    TEST_ASSERT_TRUE(ds3231_parse_datetime("20280229120000", &t));
    TEST_ASSERT_TRUE(ds3231_parse_datetime("20000229120000", &t));
}

// The sketch's Z checked only the length and wrote whatever digits it got.
static void test_parse_rejects_everything_else(void)
{
    static const char *const bad[] = {
        "19991231235959", "21000101000000",                   // years outside 2000 to 2099
        "20261313174509", "20260013174509",                   // month 13, month 0
        "20261100174509", "20261131174509", "20260230120000", // day 0, 31 November, 30 February
        "20270229120000", "21000229120000",                   // 29 February in common years
        "20261113244509", "20261113176009", "20261113174560", // hour 24, minute 60, second 60
        "2026111317450", "202611131745090", "",               // too short, too long, empty
        "2026111317450a", "2026-11-13T17:", "20261113174509\r", " 20261113174509",
        "20261113 74509", "-2026111317450",
        // The characters just outside '0'-'9', where they would still give a valid second.
        "2026111317451/", "2026111317451:",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ds3231_datetime_t t = DT(1, 1, 1, 1, 1, 1);
        TEST_ASSERT_FALSE_MESSAGE(ds3231_parse_datetime(bad[i], &t), bad[i]);
        assert_datetime(DT(1, 1, 1, 1, 1, 1), t);
    }
}

static void test_valid_checks_every_field(void)
{
    TEST_ASSERT_TRUE(ds3231_datetime_valid(&DT(2026, 4, 30, 0, 0, 0)));
    TEST_ASSERT_FALSE(ds3231_datetime_valid(&DT(2026, 4, 31, 0, 0, 0)));
    TEST_ASSERT_FALSE(ds3231_datetime_valid(&DT(2026, 4, 30, -1, 0, 0)));
    TEST_ASSERT_FALSE(ds3231_datetime_valid(&DT(2026, 4, 30, 0, -1, 0)));
    TEST_ASSERT_FALSE(ds3231_datetime_valid(&DT(2026, 4, 30, 0, 0, -1)));
    TEST_ASSERT_FALSE(ds3231_datetime_valid(&DT(2026, 4, 0, 0, 0, 0)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_matches_the_datasheet_layout);
    RUN_TEST(test_decode_reads_the_datasheet_layout);
    RUN_TEST(test_decode_12_hour_mode);
    RUN_TEST(test_decode_24_hour_mode);
    RUN_TEST(test_decode_rejects_impossible_registers);
    RUN_TEST(test_decode_29_february_only_in_leap_years);
    RUN_TEST(test_decode_power_on_reset_value);
    RUN_TEST(test_every_day_round_trips_and_matches_the_host);
    RUN_TEST(test_parse_accepts_real_dates);
    RUN_TEST(test_parse_rejects_everything_else);
    RUN_TEST(test_valid_checks_every_field);
    return UNITY_END();
}
