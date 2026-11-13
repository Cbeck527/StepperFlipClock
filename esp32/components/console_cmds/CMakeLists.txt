idf_component_register(SRCS "console_cmds.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES console flip_logic freertos stepper)
