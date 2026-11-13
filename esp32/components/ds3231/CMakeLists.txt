# ds3231_regs.c is plain C with no ESP-IDF headers, so host_test/ can build it on a laptop too.
idf_component_register(SRCS "ds3231.c" "ds3231_regs.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_i2c)
