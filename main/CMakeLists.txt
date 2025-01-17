idf_component_register(
    SRCS "test_sender.c" "oneshot_read_main.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_adc esp_wifi nvs_flash
)