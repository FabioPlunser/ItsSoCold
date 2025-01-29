#ifndef RTC_STORE_H
#define RTC_STORE_H

#include <esp_wifi.h>

// Define the NVS namespace
#define RTC_STORE_NAMESPACE "storage"

typedef struct {
    uint32_t crc;
    struct {
        int boot_count;
        int measurement_count;
        uint64_t first_measurement_time;
        float calibrated_resistor;
        wifi_config_t wifi_config;
    } data;
} rtc_store_t;

void init_rtc_data(void);
void update_rtc_data(int boot_count, int measurement_count, 
                    uint64_t first_measurement_time, float calibrated_resistor);
bool is_rtc_data_valid(void);
void backup_to_nvs(void);
bool restore_from_nvs(void);

extern rtc_store_t rtc_store;

#endif // RTC_STORE_H