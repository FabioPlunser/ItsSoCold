#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "ADC_EXAMPLE";

void app_main(void)
{
    // ADC1 Init Configuration
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // ADC1 Channel Configuration
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &config));

    // Main loop
    while (1) {
        int adc_raw;
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &adc_raw));
        ESP_LOGI(TAG, "ADC1 Channel[0] Raw Data: %d", adc_raw);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}