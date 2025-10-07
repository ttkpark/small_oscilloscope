idf_component_register(SRCS 
                    "ft800.c"
                    "app_main.c"
                    "hardware_test.c" 
                    "interactive_test.c" 
                    "adc_dma_continuous.c"
                    
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_adc esp_timer) 