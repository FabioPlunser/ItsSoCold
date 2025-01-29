idf_component_register(
    SRCS "main.c" "power_manager.c" "sensor.c" "wifi_manager.c" "rtc_store.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_adc esp_wifi nvs_flash driver esp_timer
)