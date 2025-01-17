#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_system.h"
#include <math.h>
#include <time.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>

static const char *TAG = "TEMP_CALC";

// Constants
#define BETA 3976.0
#define R2 10000.0
#define T2 298.15
#define VREF 3.3
#define SERIES_RESISTOR 27000.0

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    static int retry_count = 0;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START - Attempting to connect");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (retry_count++ < 10)
        {
            ESP_LOGI(TAG, "Retry %d/10 to connect to the AP", retry_count);
            esp_wifi_connect();
        }
        else
        {
            ESP_LOGE(TAG, "Failed to connect after 10 attempts");
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char ip_str[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "Connected! IP: %s", ip_str);
        retry_count = 0;
    }
}
static void wifi_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, "Group 1");

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
            .ssid = "Fairphone",
            .password = "ruvk1524",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init finished. Connecting...");
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
             "%04d-%02d-%02d %02d:%02d:%02d+0000,1,%.4f,yes it works\n",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
             temperature);

    // DNS Resolution
    struct hostent *host = gethostbyname("pbl.permasense.uibk.ac.at");
    if (!host)
    {
        ESP_LOGE(TAG, "DNS resolution failed");
        return;
    }

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
        .sin_addr = *((struct in_addr *)host->h_addr)
    };

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        ESP_LOGE(TAG, "Connection failed");
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

void app_main(void)
{
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
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &config));

    // Main loop
    while (1)
    {
        int adc_raw;
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &adc_raw));

        // Convert ADC raw reading to resistance
        float v_out = ((float)adc_raw / 4095.0) * VREF;
        float resistance = SERIES_RESISTOR * ((VREF / v_out) - 1);

        // Calculate temperature using Beta formula
        float temperature_kelvin = 1.0 / ((1.0 / T2) + (1.0 / BETA) * log(resistance / R2));
        float temperature_celsius = temperature_kelvin - 273.15;

        // Log the temperature
        ESP_LOGI(TAG, "ADC Raw: %d, Voltage: %.2f V, Resistance: %.2f Ω, Temperature: %.2f °C",
                 adc_raw, v_out, resistance, temperature_celsius);

        // Send data
        send_data(temperature_celsius);

        vTaskDelay(pdMS_TO_TICKS(10000)); // Delay for 10 seconds
    }
}