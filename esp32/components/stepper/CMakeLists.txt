# stepper_core.c is plain C with no ESP-IDF headers, so host_test/ can build it on a laptop too.
idf_component_register(SRCS "stepper.c" "stepper_core.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_gpio esp_timer)
