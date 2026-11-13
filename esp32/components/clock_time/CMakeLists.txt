idf_component_register(SRCS "clock_time.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ds3231
                       PRIV_REQUIRES freertos log)
