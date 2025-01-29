#include "rtc_store.h"
#include "config.h"
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_crc.h>

RTC_DATA_ATTR rtc_store_t rtc_store = {
    .crc = 0,
    .data = {
        .boot_count = 0,
        .measurement_count = 0,
        .first_measurement_time = 0,
        .calibrated_resistor = SERIES_RESISTOR,
    }};

static uint32_t calculate_rtc_crc(void)
{
    return esp_crc32_le(0, (uint8_t *)&rtc_store.data, sizeof(rtc_store.data));
}

bool is_rtc_data_valid(void)
{
    if (rtc_store.data.boot_count < 0 ||
        rtc_store.data.measurement_count < 0 ||
        rtc_store.data.measurement_count > REQUIRED_MEASUREMENTS ||
        rtc_store.data.calibrated_resistor <= 0)
    {
        return false;
    }

    uint32_t calculated_crc = calculate_rtc_crc();
    return (calculated_crc == rtc_store.crc);
}

void update_rtc_data(int boot_count, int measurement_count,
                     uint64_t first_measurement_time, float calibrated_resistor)
{
    rtc_store.data.boot_count = boot_count;
    rtc_store.data.measurement_count = measurement_count;
    rtc_store.data.first_measurement_time = first_measurement_time;
    rtc_store.data.calibrated_resistor = calibrated_resistor;
    rtc_store.crc = calculate_rtc_crc();
}

// Initialize RTC data
void init_rtc_data(void)
{
    if (!is_rtc_data_valid())
    {
        ESP_LOGI(TAG_PM, "RTC data invalid, attempting restore from NVS");
        if (!restore_from_nvs())
        {
            ESP_LOGI(TAG_PM, "NVS restore failed, resetting to defaults");
            // Reset to defaults
            update_rtc_data(0, 0, 0, SERIES_RESISTOR);
        }
    }
}
// Backup data to NVS
void backup_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(RTC_STORE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_PM, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(nvs_handle, "rtc_data", &rtc_store.data, sizeof(rtc_store.data));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_PM, "Error writing to NVS: %s", esp_err_to_name(err));
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_PM, "Error committing NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
}

// Restore data from NVS
bool restore_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(RTC_STORE_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_PM, "Error opening NVS handle: %s", esp_err_to_name(err));
        return false;
    }

    size_t required_size = sizeof(rtc_store.data);
    err = nvs_get_blob(nvs_handle, "rtc_data", &rtc_store.data, &required_size);
    nvs_close(nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_PM, "Error reading from NVS: %s", esp_err_to_name(err));
        return false;
    }

    rtc_store.crc = calculate_rtc_crc();
    return true;
}
