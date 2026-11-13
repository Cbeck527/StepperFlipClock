# Plain C with no ESP-IDF headers, so host_test/ can build it on a laptop too.
idf_component_register(SRCS "flip_logic.c"
                       INCLUDE_DIRS "include")
