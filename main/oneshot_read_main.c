#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_sntp.h"
#include <sys/socket.h>
#include <arpa/inet.h>

static const char *TAG = "TEMP_CALC";
static bool wifi_connected = false;

// Sleep configurations
#define DEEP_SLEEP_TIME_SEC 10 // Time to stay in deep sleep
#define LIGHT_SLEEP_TIME_SEC 1 // Time to stay in light sleep

// Wifi data
#define WATCHDOG_TIMEOUT_SEC 30
#define WIFI_CONNECT_TIMEOUT_MS 5000
#define WIFI_MAXIMUM_RETRY 10
#define DEVICE_NAME "Group 1"
#define WIFI_SSID "lpsd"
#define WIFI_PASS "lpsd2024"

// Data message
#define SNTP_SERVER "pool.ntp.org"
#define DATA_MESSAGE "Group 1 Temperature Sensor"
#define SERVER_IP_ADDR "138.232.18.37"

// Constants
#define BETA 3976.0
#define R2 10000.0
#define T2 298.15
#define VREF 3.3
#define SERIES_RESISTOR 15000.0

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, SNTP_SERVER);
    esp_sntp_init();
    
    // Wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 15;
    
    while (timeinfo.tm_year < (2024 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Connection failed, retrying...");
        esp_wifi_connect();
        wifi_connected = false;
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        wifi_connected = true;
        ESP_LOGI(TAG, "Connected to WiFi network");
    }
}
static void wifi_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, DEVICE_NAME);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA_PSK,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    int retry_count = 0;
    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while (!wifi_connected &&
           ((xTaskGetTickCount() * portTICK_PERIOD_MS) - start_time) < WIFI_CONNECT_TIMEOUT_MS)
    {
        esp_task_wdt_reset(); // Reset watchdog

        if (retry_count >= WIFI_MAXIMUM_RETRY)
        {
            ESP_LOGE(TAG, "WiFi connection failed after maximum retries");
            return;
        }

        ESP_LOGI(TAG, "Connecting to WiFi... (attempt %d/%d)",
                 retry_count + 1, WIFI_MAXIMUM_RETRY);

        vTaskDelay(pdMS_TO_TICKS(1000)); // Allow other tasks to run
        esp_task_wdt_reset();            // Reset watchdog again

        retry_count++;
    }
    if (!wifi_connected)
    {
        ESP_LOGE(TAG, "WiFi connection timeout");
        wifi_connected = false;
        return;
    }

    ESP_LOGI(TAG, "WiFi connected successfully");
    initialize_sntp();
    wifi_connected = true;
}

static void send_data(float temperature)
{
    // Format data string (simpler version)
    char post_data[128];
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Format: YYYY-MM-DD HH:MM:SS+0000,GROUP_ID,TEMPERATURE,COMMENT
    snprintf(post_data, sizeof(post_data),
             "%04d-%02d-%02d %02d:%02d:%02d+0000,1,%.4f,%s\n",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
             temperature, DATA_MESSAGE);

    // Create and configure socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Socket creation error");
        return;
    }

    // Connect
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(22504),
        .sin_addr.s_addr = inet_addr(SERVER_IP_ADDR)};

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        ESP_LOGE(TAG, "Connection failed: %d", errno);
        close(sock);
        return;
    }

    // Send data
    if (send(sock, post_data, strlen(post_data), 0) < 0)
    {
        ESP_LOGE(TAG, "Send failed");
    }
    else
    {
        ESP_LOGI(TAG, "Data sent: %s", post_data);
    }

    close(sock);
}

// Power management functions
void enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", DEEP_SLEEP_TIME_SEC);

    // Disconnect WiFi
    esp_wifi_stop();
    wifi_connected = false;
    // Configure wake-up timer
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_TIME_SEC * 1000000ULL);

    // Enable wake-up on GPIO if needed (example for GPIO5)
    // esp_sleep_enable_ext0_wakeup(GPIO_NUM_5, 1);

    esp_deep_sleep_start();
}

void enter_light_sleep(void)
{
    ESP_LOGI(TAG, "Preparing for light sleep");
    
    // Configure wake-up timer with longer duration
    esp_err_t err = esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_TIME_SEC * 1000000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable timer wakeup: %s", esp_err_to_name(err));
        return;
    }
    
    // Enable WiFi retention
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    ESP_ERROR_CHECK(esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON));
    
    ESP_LOGI(TAG, "Entering light sleep for %d seconds", LIGHT_SLEEP_TIME_SEC);
    
    // Enter light sleep with error checking
    esp_err_t sleep_result = esp_light_sleep_start();
    
    switch (sleep_result) {
        case ESP_OK:
            ESP_LOGI(TAG, "Woke up successfully from light sleep");
            break;
        case ESP_ERR_SLEEP_REJECT:
            ESP_LOGE(TAG, "Sleep request rejected");
            break;
        case ESP_ERR_SLEEP_TOO_SHORT_SLEEP_DURATION:
            ESP_LOGW(TAG, "Sleep duration too short, increasing");
            // Try again with longer duration
            esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_TIME_SEC * 2000000ULL);
            esp_light_sleep_start();
            break;
        default:
            ESP_LOGE(TAG, "Unexpected error in light sleep: %s", esp_err_to_name(sleep_result));
            break;
    }
    
    // Wait for system to stabilize after wakeup
    vTaskDelay(pdMS_TO_TICKS(100));
}

// Function to handle measurements and data sending
static esp_err_t measure_and_send(adc_oneshot_unit_handle_t adc1_handle)
{
    int adc_raw;
    esp_err_t ret = adc_oneshot_read(adc1_handle, ADC_CHANNEL_2, &adc_raw);
    if (ret != ESP_OK)
    {
        return ret;
    }

    // Convert ADC raw reading to resistance
    float v_out = ((float)adc_raw / 4095.0) * VREF;
    float resistance = SERIES_RESISTOR * v_out / (VREF - v_out);

    // Calculate temperature using Beta formula
    float temperature_kelvin = BETA / (log(resistance / R2) + (BETA / T2));
    float temperature_celsius = temperature_kelvin - 273.15;

    // Log the temperature
    ESP_LOGI(TAG, "Temperature: %.2f °C", temperature_celsius);
    ESP_LOGI(TAG, "Resistance: %.2f Ohm", resistance);

    // Send data
    send_data(temperature_celsius);

    return ESP_OK;
}

void app_main(void)
{
    // Initialize watchdog first
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true};
    // Delete existing watchdog if any
    if (esp_task_wdt_deinit() == ESP_OK)
    {
        ESP_LOGI(TAG, "Previous watchdog deinitialized");
    }

    ESP_ERROR_CHECK(esp_task_wdt_init(&wdt_config));
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    // Initialize Wi-Fi
    wifi_init();

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
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_2, &config));

    while (1)
    {
        esp_task_wdt_reset(); // Reset watchdog in main loop

        if (!wifi_connected)
        {
            wifi_init();
        }

        esp_err_t result = measure_and_send(adc1_handle);
        esp_task_wdt_reset(); // Reset after measurement

        if (result == ESP_OK)
        {
            esp_task_wdt_delete(NULL); // Remove watchdog before sleep

            // Option 1: Deep sleep (WiFi disconnects, full reboot on wake)
            enter_deep_sleep();

            // Option 2: Light sleep (maintains WiFi, faster wake-up)
            //enter_light_sleep();
            ESP_ERROR_CHECK(esp_task_wdt_add(NULL)); // Re-add watchdog after wake
        }
        else
        {
            ESP_LOGE(TAG, "Measurement failed");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}