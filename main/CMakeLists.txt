idf_component_register(SRCS "analog_test_simple.c" "ft800.c" "app_main.c" "oscilloscope_test.c" "hardware_test.c" "interactive_test.c" "adc_dma_continuous.c" "adc_dma_test.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_adc esp_timer) 