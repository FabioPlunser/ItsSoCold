#include "config.h"
#include "rtc_store.h"
#include "wifi_manager.h"
#include "sensor.h"
#include "power_manager.h"
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_log.h>

typedef enum
{
    STATE_IDLE,
    STATE_MEASURING,
    STATE_SLEEPING
} system_state_t;

static void init_buttons(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_CALIBRATE) | (1ULL << BUTTON_START),
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static adc_oneshot_unit_handle_t init_adc(void)
{
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_2, &config));
    return adc1_handle;
}

static void handle_measurements(adc_oneshot_unit_handle_t adc1_handle)
{
    ESP_LOGI(TAG_ADC, "Starting measurement cycle");
    esp_task_wdt_reset();

    // Try to connect to WiFi with retries
    int wifi_retry = 0;
    const int max_wifi_retries = 3;

    while (!wifi_connected && wifi_retry < max_wifi_retries)
    {
        wifi_quick_connect();
        if (wifi_connected)
        {
            break;
        }
        wifi_retry++;
        ESP_LOGI(TAG_WIFI, "WiFi connection attempt %d/%d failed", wifi_retry, max_wifi_retries);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (wifi_connected)
    {
        esp_err_t result = measure_and_send(adc1_handle);
        if (result == ESP_OK)
        {
            ESP_LOGI(TAG_PM, "Measurement successful");
            // Make sure WiFi is properly stopped
            esp_wifi_disconnect();
            esp_wifi_stop();
            esp_wifi_deinit();

            adc_oneshot_del_unit(adc1_handle);
            esp_task_wdt_delete(NULL);
            enter_deep_sleep();
        }
        else
        {
            ESP_LOGI(TAG_ADC, "Measurement failed with error: %d", result);
            // Reset measurement count on failure
            update_rtc_data(rtc_store.data.boot_count,
                            0,
                            0,
                            rtc_store.data.calibrated_resistor);
            esp_wifi_stop();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    else
    {
        ESP_LOGI(TAG_WIFI, "Failed to connect to WiFi after multiple attempts");
        // Reset boot count to force full WiFi initialization next time
        update_rtc_data(0,
                        rtc_store.data.measurement_count,
                        rtc_store.data.first_measurement_time,
                        rtc_store.data.calibrated_resistor);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    // Initialize components
    init_nvs();
    init_rtc_data();
    init_watchdog();
    adc_oneshot_unit_handle_t adc1_handle = init_adc();
    init_buttons();
    system_state_t current_state = STATE_IDLE;
    bool start_measurements = false;
    if (!is_fresh_start())
    {
        current_state = STATE_MEASURING;
        start_measurements = true;
    }

    while (1)
    {
        esp_task_wdt_reset();

        switch (current_state)
        {
        case STATE_IDLE:
            // Handle calibration button
            if (gpio_get_level(BUTTON_CALIBRATE) == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
                if (gpio_get_level(BUTTON_CALIBRATE) == 0)
                {
                    calibrate_sensor(adc1_handle);
                    // Wait for button release
                    while (gpio_get_level(BUTTON_CALIBRATE) == 0)
                    {
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }
                    // Add delay after calibration
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
            }

            // Handle start button
            if (gpio_get_level(BUTTON_START) == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
                if (gpio_get_level(BUTTON_START) == 0)
                {
                    // Reset measurement counters
                    update_rtc_data(rtc_store.data.boot_count,
                                    0,
                                    0,
                                    rtc_store.data.calibrated_resistor);
                    start_measurements = true;
                    current_state = STATE_MEASURING;
                    // Wait for button release
                    while (gpio_get_level(BUTTON_START) == 0)
                    {
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case STATE_MEASURING:
            if (start_measurements)
            {
                handle_measurements(adc1_handle);
                current_state = STATE_SLEEPING;
            }
            break;

        case STATE_SLEEPING:
            enter_deep_sleep();
            break;
        }
    }
}