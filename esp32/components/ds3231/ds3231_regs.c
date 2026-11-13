#include "ds3231_regs.h"

// Exact for 2000 to 2099, the only years allowed, and the rule the DS3231 itself uses.
static bool is_leap(int year)
{
    return year % 4 == 0;
}

static int days_in_month(int year, int month)
{
    static const int DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && is_leap(year) ? 29 : DAYS[month - 1];
}

bool ds3231_datetime_valid(const ds3231_datetime_t *t)
{
    return t->year >= 2000 && t->year <= 2099 && t->month >= 1 && t->month <= 12 &&
           t->day >= 1 && t->day <= days_in_month(t->year, t->month) &&
           t->hour >= 0 && t->hour <= 23 && t->minute >= 0 && t->minute <= 59 &&
           t->second >= 0 && t->second <= 59;
}

static int digits(const char *text, int count)
{
    int value = 0;
    for (int i = 0; i < count; i++) {
        value = 10 * value + (text[i] - '0');
    }
    return value;
}

bool ds3231_parse_datetime(const char *text, ds3231_datetime_t *t)
{
    for (int i = 0; i < 14; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false; // Also stops at a terminating '\0' before the fourteenth digit.
        }
    }
    if (text[14] != '\0') {
        return false;
    }
    ds3231_datetime_t parsed = {
        .year = digits(text, 4),
        .month = digits(text + 4, 2),
        .day = digits(text + 6, 2),
        .hour = digits(text + 8, 2),
        .minute = digits(text + 10, 2),
        .second = digits(text + 12, 2),
    };
    if (!ds3231_datetime_valid(&parsed)) {
        return false;
    }
    *t = parsed;
    return true;
}

static uint8_t to_bcd(int value)
{
    return (uint8_t)((value / 10) << 4 | value % 10);
}

// Decodes a BCD register, or returns -1 when the units digit is above 9. A set bit
// above a field's digits, including the century bit, makes the tens digit and so the
// value too large for that field, so validation refuses it.
static int from_bcd(uint8_t reg)
{
    if ((reg & 0x0f) > 9) {
        return -1;
    }
    return (reg >> 4) * 10 + (reg & 0x0f);
}

// Days since 1970-01-01 for a date in the proleptic Gregorian calendar, from Howard
// Hinnant's days_from_civil, with March as the first month of the year so that
// 29 February comes last.
static int64_t days_from_civil(int year, int month, int day)
{
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    int64_t year_of_era = year - era * 400;
    int64_t day_of_year = (153 * ((month + 9) % 12) + 2) / 5 + day - 1;
    int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

void ds3231_encode_time(const ds3231_datetime_t *t, uint8_t regs[DS3231_TIME_LEN])
{
    // 1970-01-01 was a Thursday, day 5 when 1 is Sunday.
    int64_t days = days_from_civil(t->year, t->month, t->day);
    regs[0] = to_bcd(t->second);
    regs[1] = to_bcd(t->minute);
    regs[2] = to_bcd(t->hour); // 24-hour mode: bit 6 clear.
    regs[3] = (uint8_t)((days + 4) % 7 + 1);
    regs[4] = to_bcd(t->day);
    regs[5] = to_bcd(t->month); // Century bit clear: 2000 to 2099.
    regs[6] = to_bcd(t->year - 2000);
}

bool ds3231_decode_time(const uint8_t regs[DS3231_TIME_LEN], ds3231_datetime_t *t)
{
    int hour;
    if (regs[2] & DS3231_HOURS_12H) {
        int hour12 = from_bcd(regs[2] & ~(DS3231_HOURS_12H | DS3231_HOURS_PM));
        if (hour12 < 1 || hour12 > 12) {
            return false;
        }
        // 12 AM is 00, 12 PM is 12.
        hour = hour12 % 12 + (regs[2] & DS3231_HOURS_PM ? 12 : 0);
    } else {
        hour = from_bcd(regs[2]);
    }
    if (regs[3] < 1 || regs[3] > 7) {
        return false;
    }
    int year = from_bcd(regs[6]);
    ds3231_datetime_t decoded = {
        .year = year < 0 ? -1 : 2000 + year,
        .month = from_bcd(regs[5]), // With the century bit set, this is 81 or more.
        .day = from_bcd(regs[4]),
        .hour = hour,
        .minute = from_bcd(regs[1]),
        .second = from_bcd(regs[0]),
    };
    if (!ds3231_datetime_valid(&decoded)) {
        return false;
    }
    *t = decoded;
    return true;
}

int64_t ds3231_datetime_to_unix(const ds3231_datetime_t *t)
{
    return days_from_civil(t->year, t->month, t->day) * 86400 + t->hour * 3600 + t->minute * 60 +
           t->second;
}

void ds3231_datetime_from_unix(int64_t unix_seconds, ds3231_datetime_t *t)
{
    // Howard Hinnant's civil_from_days.
    int64_t days = unix_seconds / 86400;
    int64_t seconds = unix_seconds % 86400;
    if (seconds < 0) {
        seconds += 86400;
        days--;
    }
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    int64_t day_of_era = days - era * 146097;
    int64_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    int64_t mp = (5 * day_of_year + 2) / 153; // Month counted from March.
    int month = (int)(mp < 10 ? mp + 3 : mp - 9);
    *t = (ds3231_datetime_t){
        .year = (int)(year_of_era + era * 400 + (month <= 2)),
        .month = month,
        .day = (int)(day_of_year - (153 * mp + 2) / 5 + 1),
        .hour = (int)(seconds / 3600),
        .minute = (int)(seconds / 60 % 60),
        .second = (int)(seconds % 60),
    };
}
