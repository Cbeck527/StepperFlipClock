idf_component_register(SRCS "console_cmds.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES clock_task clock_time console ds3231 flip_logic freertos stepper)
