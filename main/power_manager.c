#include "power_manager.h"
#include "config.h"
#include "rtc_store.h"
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <esp_log.h>

RTC_DATA_ATTR uint32_t esp_reset_count = 0;

bool is_fresh_start(void)
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    // If it's not a deep sleep wakeup, increment reset count
    if (wakeup_reason != ESP_SLEEP_WAKEUP_TIMER)
    {
        rtc_store.data.boot_count = 0;
        esp_reset_count++;
        return true;
    }
    return false;
}

void init_watchdog(void)
{
    if (esp_task_wdt_deinit() == ESP_OK)
    {
        ESP_LOGI(TAG_PM, "Previous watchdog deinitialized");
    }
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true};
    ESP_ERROR_CHECK(esp_task_wdt_init(&wdt_config));
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
}

void enter_deep_sleep(void)
{
    if (rtc_store.data.measurement_count >= REQUIRED_MEASUREMENTS)
    {
        ESP_LOGI(TAG_PM, "Completed %d measurements. Going to extended sleep.",
                 REQUIRED_MEASUREMENTS);
        // Reset measurement count and first measurement time
        update_rtc_data(rtc_store.data.boot_count,
                        0, // Reset measurement count
                        0, // Reset first measurement time
                        rtc_store.data.calibrated_resistor);
        esp_deep_sleep(MEASUREMENT_WINDOW_SEC * 1000000ULL);
    }
    else
    {
        ESP_LOGI(TAG_PM, "Measurement %d/%d completed. Short sleep.",
                 rtc_store.data.measurement_count, REQUIRED_MEASUREMENTS);
        // Keep the current measurement count
        update_rtc_data(rtc_store.data.boot_count,
                        rtc_store.data.measurement_count,
                        rtc_store.data.first_measurement_time,
                        rtc_store.data.calibrated_resistor);
        esp_deep_sleep(DEEP_SLEEP_TIME_SEC * 1000000ULL);
    }
}
