# display_store_format.c is plain C with no ESP-IDF headers, so host_test/ can build it on a laptop too.
idf_component_register(SRCS "display_store.c" "display_store_format.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES ds3231)
