#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include <math.h>

static const char *TAG = "TEMP_CALC";

// Constants
#define BETA 3976.0             // Beta value of the thermistor
#define R2 10000.0              // Resistance at reference temperature (25°C)
#define T2 298.15               // Reference temperature in Kelvin (25°C)
#define VREF 3.3                // Reference voltage (ESP32 power supply)
#define SERIES_RESISTOR 10000.0 // Series resistor value in the voltage divider

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
    while (1)
    {
        int adc_raw;
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &adc_raw));

        // Convert ADC raw reading to resistance
        float v_out = ((float)adc_raw / 4095.0) * VREF;            // Convert ADC reading to voltage
        float resistance = SERIES_RESISTOR * ((VREF / v_out) - 1); // Calculate thermistor resistance

        // Calculate temperature using Beta formula
        float temperature_kelvin = 1.0 / ((1.0 / T2) + (1.0 / BETA) * log(resistance / R2));
        float temperature_celsius = temperature_kelvin - 273.15; // Convert to Celsius

        // Log the temperature
        ESP_LOGI(TAG, "ADC Raw: %d, Voltage: %.2f V, Resistance: %.2f Ω, Temperature: %.2f °C",
                 adc_raw, v_out, resistance, temperature_celsius);

        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1 second
    }
}
