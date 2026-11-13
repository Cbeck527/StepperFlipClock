#pragma once

// DS3231 register layout and the conversions between its registers, a calendar date
// and time, and Unix time, in plain C. No ESP-IDF headers here: host_test/ builds this
// on the host. ds3231.c moves the bytes over I2C. Section and figure numbers refer to
// the Maxim/Analog Devices DS3231 datasheet, 19-5170 Rev 10 (3/15).

#include <stdbool.h>
#include <stdint.h>

#define DS3231_I2C_ADDRESS 0x68

// Timekeeping registers 00h to 06h: seconds, minutes, hours, day, date, month/century, year,
// in BCD. Figure 1, Timekeeping Registers, and section Clock and Calendar.
#define DS3231_REG_TIME 0x00
#define DS3231_TIME_LEN 7
#define DS3231_HOURS_12H 0x40     // Hours register, bit 6: 1 selects 12-hour mode.
#define DS3231_HOURS_PM 0x20      // Hours register, bit 5 in 12-hour mode: 1 is PM.
#define DS3231_MONTH_CENTURY 0x80 // Month register, bit 7: toggles when the year wraps from 99 to 00.

// Alarm 2 registers 0Bh to 0Dh: minutes, hours, day/date. Figure 1, and section Alarms with Table 2.
#define DS3231_REG_ALARM2 0x0B
#define DS3231_ALARM2_LEN 3

// Control register 0Eh. Section Special-Purpose Registers, Control Register (0Eh).
#define DS3231_REG_CONTROL 0x0E
#define DS3231_CONTROL_EOSC 0x80  // 1 stops the oscillator when running on the backup battery.
#define DS3231_CONTROL_INTCN 0x04 // 1 makes INT/SQW an alarm interrupt output instead of a square wave.
#define DS3231_CONTROL_A2IE 0x02  // Alarm 2 interrupt enable.
#define DS3231_CONTROL_A1IE 0x01  // Alarm 1 interrupt enable.

// Status register 0Fh. Section Special-Purpose Registers, Status Register (0Fh).
#define DS3231_REG_STATUS 0x0F
#define DS3231_STATUS_OSF 0x80 // Oscillator stop flag: the oscillator stopped at some point, so the time is wrong.

// Registers 00h to 0Fh, read in one transaction so the time and the status agree. A read
// starting at 00h comes from a copy taken at the START condition (section Address Map).
#define DS3231_REGS_LEN 16

// A UTC date and time. The DS3231 handles leap years correctly only up to 2099, so
// valid years are 2000 to 2099.
typedef struct {
    int year;   // 2000 to 2099
    int month;  // 1 to 12
    int day;    // 1 to the length of the month
    int hour;   // 0 to 23
    int minute; // 0 to 59
    int second; // 0 to 59
} ds3231_datetime_t;

// True when every field is in range and the date exists, such as 29 February only in leap years.
bool ds3231_datetime_valid(const ds3231_datetime_t *t);

// Parses exactly 14 digits, yyyymmddhhmmss, such as "20261113174509". Returns false,
// leaving *t alone, for anything else or for a date or time that does not exist.
bool ds3231_parse_datetime(const char *text, ds3231_datetime_t *t);

// Encodes a valid date and time into registers 00h to 06h, in 24-hour mode with the
// century bit clear. The day-of-week register gets 1 for Sunday to 7 for Saturday.
void ds3231_encode_time(const ds3231_datetime_t *t, uint8_t regs[DS3231_TIME_LEN]);

// Decodes registers 00h to 06h, in 12- or 24-hour mode. Returns false, leaving *t
// alone, when a digit is not BCD, a field is out of range, the date does not exist, a bit
// the datasheet shows as 0 is set, or the century bit is set (a year past 2099). The
// day-of-week register's value is ignored.
bool ds3231_decode_time(const uint8_t regs[DS3231_TIME_LEN], ds3231_datetime_t *t);

// Seconds since 1970-01-01 00:00:00 UTC for a valid date and time, and back.
int64_t ds3231_datetime_to_unix(const ds3231_datetime_t *t);
void ds3231_datetime_from_unix(int64_t unix_seconds, ds3231_datetime_t *t);
