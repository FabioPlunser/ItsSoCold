idf_component_register(
    SRCS "oneshot_read_main.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_adc esp_wifi nvs_flash driver esp_timer
)