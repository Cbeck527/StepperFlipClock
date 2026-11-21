#pragma once
// Prints like ESP-IDF's log, and counts error lines so tests can check how often they appear.
#include <stdio.h>
extern int stub_log_errors;
#define ESP_LOGE(tag, fmt, ...) (stub_log_errors++, printf("E %s: " fmt "\n", tag, ##__VA_ARGS__))
#define ESP_LOGW(tag, fmt, ...) printf("W %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("I %s: " fmt "\n", tag, ##__VA_ARGS__)
