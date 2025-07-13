idf_component_register(SRCS "ft800.c" "app_main.c" "oscilloscope_test.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_adc esp_timer) 