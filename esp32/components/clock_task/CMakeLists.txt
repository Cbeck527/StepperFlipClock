idf_component_register(SRCS "clock_task.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES clock_time display_store flip_logic freertos log stepper)
