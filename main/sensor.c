#include "sensor.h"
#include "config.h"
#include "rtc_store.h"
#include "wifi_manager.h"
#include <esp_log.h>
#include <math.h>
#include <esp_timer.h>

void calibrate_sensor(adc_oneshot_unit_handle_t adc1_handle)
{
    ESP_LOGI(TAG_ADC, "Starting calibration at 0°C...");

    // Take multiple readings
    int32_t adc_sum = 0;
    for (int i = 0; i < ADC_SAMPLES * 2; i++)
    {
        int raw_value;
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_2, &raw_value));
        adc_sum += raw_value;
        vTaskDelay(pdMS_TO_TICKS(ADC_SAMPLE_DELAY_MS));
    }

    float raw_value = (float)adc_sum / (ADC_SAMPLES * 2);
    float v_out = (raw_value / ADC_MAX_VALUE) * VREF;

    // Calculate new series resistor value for 0°C (273.15K)
    float r_thermistor = R2 * exp((BETA / 273.15) - (BETA / T2));
    float new_resistor = (r_thermistor * (VREF - v_out)) / v_out;

    // Update RTC store with new calibrated value
    update_rtc_data(rtc_store.data.boot_count,
                    0, // Reset measurement count after calibration
                    0, // Reset first measurement time
                    new_resistor);

    ESP_LOGI(TAG_ADC, "Calibration complete. New resistor value: %.2f", rtc_store.data.calibrated_resistor);

    // Backup to NVS immediately after calibration
    backup_to_nvs();
}

// Function to handle measurements and data sending
esp_err_t measure_and_send(adc_oneshot_unit_handle_t adc1_handle)
{
    if (adc1_handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    int32_t adc_sum = 0;
    int32_t real_number_of_samples = 0;
    for (int i = 0; i < ADC_SAMPLES; i++)
    {
        int raw_value;
        esp_err_t ret = adc_oneshot_read(adc1_handle, ADC_CHANNEL_2, &raw_value);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG_ADC, "ADC read error: %d", ret);
            return ret;
        }
        if (raw_value < 0 || raw_value > ADC_MAX_VALUE)
        {
            ESP_LOGE(TAG_ADC, "Invalid ADC reading: %d", raw_value);
            continue;
        }
        real_number_of_samples++;
        adc_sum += raw_value;
        vTaskDelay(pdMS_TO_TICKS(ADC_SAMPLE_DELAY_MS));
    }
    if (real_number_of_samples == 0)
    {
        ESP_LOGE(TAG_ADC, "No valid ADC readings");
        return ESP_ERR_INVALID_RESPONSE;
    }
    float raw_value = (float)adc_sum / real_number_of_samples;

    // Convert to temperature
    float v_out = (raw_value / ADC_MAX_VALUE) * VREF;
    float resistance = rtc_store.data.calibrated_resistor * v_out / (VREF - v_out);
    float temperature_kelvin = BETA / (log(resistance / R2) + (BETA / T2));
    float temperature_celsius = temperature_kelvin - KELVIN_TO_CELSIUS;

    ESP_LOGI(TAG_TEMP, "Temperature: %.2f°C", temperature_celsius);

    // Track first measurement time
    if (rtc_store.data.measurement_count == 0)
    {
        rtc_store.data.first_measurement_time = esp_timer_get_time();
    }

    // Increment measurement count
    rtc_store.data.measurement_count++;

    // Calculate elapsed time in seconds
    uint64_t elapsed_time = (esp_timer_get_time() - rtc_store.data.first_measurement_time) / 1000000;

    ESP_LOGI(TAG_TEMP, "Measurement %d/10 (Elapsed: %lld sec)",
             rtc_store.data.measurement_count, elapsed_time);

    if (!initialize_sntp())
    {
        ESP_LOGE(TAG_SNTP, "Time sync failed, skipping data send");
        return ESP_OK;
    }

    // Reset counters if measurement window exceeded
    if (elapsed_time >= MEASUREMENT_WINDOW_SEC)
    {
        rtc_store.data.measurement_count = 0;
        rtc_store.data.first_measurement_time = 0;
    }
#ifdef SEND_DATA
    return send_data(temperature_celsius);
#else
    return ESP_OK;
#endif
}
