// config.h
#ifndef CONFIG_H
#define CONFIG_H

// WiFi configurations
#define WIFI_CONNECT_TIMEOUT_MS 100000
#define WIFI_MAXIMUM_RETRY 5
#define DEVICE_NAME "Group 1"
#define WIFI_SSID "Beste"
#define WIFI_PASS "PumaTec2612"
#define WIFI_AUTH WIFI_AUTH_WPA2_PSK

// Server configurations
#define SNTP_SERVER "pool.ntp.org"
#define DATA_MESSAGE "Group 1 Temperature Sensor"
#define SERVER_IP_ADDR "138.232.18.37"

// Sensor configurations
#define BETA 3976.0
#define R2 10000.0
#define T2 298.15
#define VREF 3.3
#define SERIES_RESISTOR 15000.0

// ADC configurations
#define ADC_SAMPLES 5
#define ADC_SAMPLE_DELAY_MS 10
#define ADC_MAX_VALUE 4095
#define KELVIN_TO_CELSIUS 273.15f

// System configurations
#define DEEP_SLEEP_TIME_SEC 30
#define REQUIRED_MEASUREMENTS 10
#define MEASUREMENT_WINDOW_SEC 300
#define WATCHDOG_TIMEOUT_SEC 30
#define RETRY_DELAY_MS 1000

// GPIO configurations
#define BUTTON_CALIBRATE GPIO_NUM_23
#define BUTTON_START GPIO_NUM_19
#define BUTTON_DEBOUNCE_MS 50

// Log tags
#define TAG_WIFI "wifi"
#define TAG_ADC "adc"
#define TAG_TEMP "temp"
#define TAG_PM "power"
#define TAG_SNTP "time"

#endif // CONFIG_H