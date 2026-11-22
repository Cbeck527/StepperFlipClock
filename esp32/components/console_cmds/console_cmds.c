#include "console_cmds.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "clock_task.h"
#include "clock_time.h"
#include "ds3231.h"
#include "esp_check.h"
#include "esp_console.h"
#include "flip_logic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linenoise/linenoise.h"
#include "stepper.h"

static const char *TAG = "console_cmds";

static uint32_t s_max_steps;
static uint32_t s_step_period_us;

static int cmd_step(int argc, char **argv)
{
    uint32_t count;
    if (argc != 2 || !flip_parse_uint(argv[1], 1, s_max_steps, &count)) {
        printf("Usage: s <count>, with count from 1 to %" PRIu32 "\n", s_max_steps);
        return 1;
    }

    stepper_add(count);
    // Wait for the burst and the coil switch-off after it, allowing twice the expected time.
    TickType_t limit = xTaskGetTickCount() +
                       pdMS_TO_TICKS(2 * (uint64_t)(count + 1) * s_step_period_us / 1000 + 1000);
    while (stepper_is_busy()) {
        if (xTaskGetTickCount() > limit) {
            printf("s %" PRIu32 ": the motor is still running, which should not happen\n", count);
            return 1;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    stepper_burst_t burst = stepper_last_burst();
    printf("s %" PRIu32 ": %" PRIu32 " steps in %" PRIu32 " ms, gap min %" PRIu32 " us, max %" PRIu32 " us\n",
           count, burst.steps, burst.span_us / 1000, burst.min_gap_us, burst.max_gap_us);
    return 0;
}

static void print_tm(const char *label, const struct tm *tm, const char *format)
{
    char text[32];
    strftime(text, sizeof text, format, tm);
    printf("%-6s %s\n", label, text);
}

static void print_display(void)
{
    char text[160];
    clock_task_describe(text, sizeof text);
    printf("%s\n", text);
}

// Prints system time in UTC and local time, what the RTC says, and whether the time is trusted.
static void print_now(void)
{
    time_t now = time(NULL);
    struct tm tm;
    print_tm("UTC", gmtime_r(&now, &tm), "%Y-%m-%d %H:%M:%S");
    print_tm("Local", localtime_r(&now, &tm), "%Y-%m-%d %H:%M:%S %Z");

    ds3231_datetime_t rtc;
    bool stopped = false;
    esp_err_t err = ds3231_read_time(&rtc, &stopped);
    if (err == ESP_OK || err == ESP_ERR_INVALID_RESPONSE) {
        if (err == ESP_OK) {
            printf("RTC    %04d-%02d-%02d %02d:%02d:%02d UTC", rtc.year, rtc.month, rtc.day, rtc.hour,
                   rtc.minute, rtc.second);
        } else {
            printf("RTC    no valid date in its registers");
        }
        printf("%s\n", stopped ? ", oscillator stop flag SET" : ", oscillator ok");
    } else {
        printf("RTC    not responding (%s)\n", esp_err_to_name(err));
    }

    if (clock_time_trusted()) {
        printf("Time is trusted.\n");
    } else {
        printf("Time is NOT trusted. Set it with: z <yyyymmddhhmmss>, in UTC\n");
    }

    print_display();
}

static int cmd_set_display(int argc, char **argv)
{
    int minute;
    if (argc != 2 || !flip_parse_hhmm(argv[1], &minute)) {
        printf("Usage: w HHMM, the time the display shows now, such as w 0905 or w 1742\n");
        return 1;
    }
    esp_err_t err = clock_task_set_display(minute);
    if (err != ESP_OK) {
        printf("Saving it in the RTC failed (%s); the clock uses it until the next restart\n",
               esp_err_to_name(err));
    }
    print_display();
    return err == ESP_OK ? 0 : 1;
}

static int cmd_hold(int argc, char **argv)
{
    int minute;
    if (argc != 2 || !flip_parse_hhmm(argv[1], &minute)) {
        printf("Usage: d HHMM, the time to move the display to and keep it at, such as d 0905\n");
        return 1;
    }
    clock_task_hold(minute);
    print_display();
    return 0;
}

static int cmd_follow(int argc, char **argv)
{
    uint32_t on;
    if (argc != 2 || !flip_parse_uint(argv[1], 0, 1, &on)) {
        printf("Usage: t 1 to follow local time, t 0 to stop the display where it is\n");
        return 1;
    }
    clock_task_follow(on == 1);
    print_display();
    return 0;
}

static int cmd_now(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("Usage: now\n");
        return 1;
    }
    print_now();
    return 0;
}

static int cmd_set_time(int argc, char **argv)
{
    ds3231_datetime_t utc;
    if (argc != 2 || !ds3231_parse_datetime(argv[1], &utc)) {
        printf("Usage: z <yyyymmddhhmmss>, a real UTC date and time from 2000 to 2099,\n"
               "for example z 20261113174509. Get it on your computer with: date -u +%%Y%%m%%d%%H%%M%%S\n");
        return 1;
    }
    esp_err_t err = clock_time_set_utc(&utc);
    if (err != ESP_OK) {
        printf("Could not set the RTC: %s\n", esp_err_to_name(err));
        return 1;
    }
    print_now();
    return 0;
}

// esp_console_new_repl_stdio() asks the terminal for its status (ESC[5n) before it installs
// the USB Serial/JTAG driver. Until then, a non-blocking read of stdin never looks at the
// hardware, so the reply is never seen, the REPL falls back to dumb mode, and the reply turns
// up later as the first line typed. Now the driver is installed, reads work: the earlier reply
// is usually waiting, and if not, asking again gets one.
void console_cmds_redo_terminal_probe(void)
{
    if (!linenoiseIsDumbMode()) {
        return; // Fixed upstream, or a console that never had the problem.
    }
    int fd = fileno(stdin);
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    // Take everything already received, for 100 ms. An escape character can only be the
    // terminal's reply, since nothing else sends one before the prompt appears.
    bool answered = false;
    for (int i = 0; i < 10; i++) {
        char c;
        while (read(fd, &c, 1) == 1) {
            answered |= c == '\x1b';
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    fcntl(fd, F_SETFL, flags);

    if (answered || linenoiseProbe() == 0) {
        linenoiseSetDumbMode(0);
        ESP_LOGI(TAG, "Terminal answered the escape-sequence probe; line editing is on");
    } else {
        ESP_LOGI(TAG, "Terminal did not answer the escape-sequence probe; line editing stays off");
    }
}

esp_err_t console_cmds_register(uint32_t max_steps, uint32_t step_period_us)
{
    s_max_steps = max_steps;
    s_step_period_us = step_period_us;
    const esp_console_cmd_t step = {
        .command = "s",
        .help = "Turn the motor <count> steps, bypassing the clock, which does not count them. 142 or 143 steps is one minute on "
                "the Copal 227. Prints the steps taken and the shortest and longest gap between steps.",
        .hint = "<count>",
        .func = cmd_step,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&step), TAG, "registering s");

    const esp_console_cmd_t now = {
        .command = "now",
        .help = "Print the time in UTC and local time, what the RTC says, whether the time is "
                "trusted, and what the display shows.",
        .func = cmd_now,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&now), TAG, "registering now");

    const esp_console_cmd_t set_time = {
        .command = "z",
        .help = "Set the RTC and the clock's time, in UTC: z yyyymmddhhmmss. "
                "On your computer, date -u +%Y%m%d%H%M%S prints it.",
        .hint = "<yyyymmddhhmmss>",
        .func = cmd_set_time,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&set_time), TAG, "registering z");

    const esp_console_cmd_t set_display = {
        .command = "w",
        .help = "Tell the clock what the display shows now, as HHMM: w 1742. The clock then moves "
                "it to local time and keeps it there. Use it after trimming with s.",
        .hint = "<HHMM>",
        .func = cmd_set_display,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&set_display), TAG, "registering w");

    const esp_console_cmd_t hold = {
        .command = "d",
        .help = "Move the display forward to HHMM and keep it there instead of following local "
                "time: d 0905. t 1 follows local time again, and so does every restart.",
        .hint = "<HHMM>",
        .func = cmd_hold,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&hold), TAG, "registering d");

    const esp_console_cmd_t follow = {
        .command = "t",
        .help = "t 1 makes the display follow local time, which it does after every restart. "
                "t 0 stops it where it is.",
        .hint = "<0|1>",
        .func = cmd_follow,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&follow), TAG, "registering t");
    return ESP_OK;
}
