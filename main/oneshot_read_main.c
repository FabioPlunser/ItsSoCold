#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include <sys/socket.h>
#include "driver/gpio.h"
#include "esp_task_wdt.h"
#include "esp_adc/adc_oneshot.h"

// Wifi data
#define WIFI_CONNECT_TIMEOUT_MS 10000
#define WIFI_MAXIMUM_RETRY 5
#define DEVICE_NAME "Group 1"
#define WIFI_SSID "lpsd"
#define WIFI_PASS "lpsd2024"
#define WIFI_AUTH WIFI_AUTH_WPA2_PSK

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

#define ADC_SAMPLES 5
#define ADC_SAMPLE_DELAY_MS 10
#define ADC_MAX_VALUE 4095
#define KELVIN_TO_CELSIUS 273.15f
#define RETRY_DELAY_MS 1000

#define TAG_WIFI "wifi"
#define TAG_ADC "adc"
#define TAG_TEMP "temp"
#define TAG_PM "power"
#define TAG_SNTP "time"

#define DEEP_SLEEP_TIME_SEC 60
#define WATCHDOG_TIMEOUT_SEC 30

#define BUTTON_CALIBRATE GPIO_NUM_23 // Calibration button
#define BUTTON_START GPIO_NUM_19     // Start measurement button
#define BUTTON_DEBOUNCE_MS 50        

// Calibration data
RTC_DATA_ATTR static float calibrated_resistor = SERIES_RESISTOR;

// Store data in RTC memory to survive deep sleep
RTC_DATA_ATTR static int boot_count = 0;
RTC_DATA_ATTR static wifi_config_t stored_wifi_config;
static bool wifi_connected = false;

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

static void calibrate_sensor(adc_oneshot_unit_handle_t adc1_handle)
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
    calibrated_resistor = (r_thermistor * (VREF - v_out)) / v_out;

    ESP_LOGI(TAG_ADC, "Calibration complete. New resistor value: %.2f", calibrated_resistor);
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG_SNTP, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, SNTP_SERVER);
    esp_sntp_init();

    // Wait for time to be set
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 15;

    while (timeinfo.tm_year < (2024 - 1900) && ++retry < retry_count)
    {
        ESP_LOGI(TAG_SNTP, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
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
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG_WIFI, "WiFi station mode starting...");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG_WIFI, "WiFi connected");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG_WIFI, "WiFi disconnected");
            wifi_connected = false;
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG_WIFI, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        initialize_sntp();
    }
}

static void wifi_init(void)
{
    static bool wifi_initialized = false;

    if (!wifi_initialized)
    {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_t *netif = esp_netif_create_default_wifi_sta();
        esp_netif_set_hostname(netif, DEVICE_NAME);

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        ESP_ERROR_CHECK(
            esp_event_handler_instance_register(WIFI_EVENT,
                                                ESP_EVENT_ANY_ID,
                                                &wifi_event_handler,
                                                NULL,
                                                NULL));
        ESP_ERROR_CHECK(
            esp_event_handler_instance_register(IP_EVENT,
                                                IP_EVENT_STA_GOT_IP,
                                                &wifi_event_handler,
                                                NULL,
                                                NULL));
        wifi_initialized = true;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    vTaskDelay(pdMS_TO_TICKS(100));

    // Connection attempt loop
    int retry_count = 0;
    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while (!wifi_connected)
    {
        esp_task_wdt_reset();

        if (((xTaskGetTickCount() * portTICK_PERIOD_MS) - start_time) >= WIFI_CONNECT_TIMEOUT_MS)
        {
            ESP_LOGE(TAG_WIFI, "WiFi connection timeout");
            return;
        }

        if (retry_count >= WIFI_MAXIMUM_RETRY)
        {
            ESP_LOGE(TAG_WIFI, "WiFi connection failed after maximum retries");
            return;
        }

        ESP_LOGI(TAG_WIFI, "Connecting to WiFi... (attempt %d/%d)",
                 retry_count + 1, WIFI_MAXIMUM_RETRY);

        vTaskDelay(pdMS_TO_TICKS(1000));
        retry_count++;
    }

    if (wifi_connected)
    {
        // Store config only on successful connection
        memcpy(&stored_wifi_config, &wifi_config, sizeof(wifi_config_t));
        boot_count++;
        ESP_LOGI(TAG_WIFI, "WiFi connected successfully, boot_count: %d", boot_count);
    }
}

static void wifi_quick_connect(void)
{
    if (boot_count > 0)
    {
        ESP_LOGI(TAG_WIFI, "Using stored WiFi config from boot %d", boot_count);
        esp_wifi_set_config(WIFI_IF_STA, &stored_wifi_config);
        esp_wifi_start();
    }
    else
    {
        // First boot or previous connection failed
        ESP_LOGI(TAG_WIFI, "First boot or reconnect needed");
        wifi_init();
    }
}

static esp_err_t send_data(float temperature)
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
        ESP_LOGE(TAG_WIFI, "Socket creation error");
        return ESP_FAIL;
    }

    // Connect
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(22504),
        .sin_addr.s_addr = inet_addr(SERVER_IP_ADDR)};

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        ESP_LOGE(TAG_WIFI, "Connection failed: %d", errno);
        close(sock);
        return ESP_ERR_TIMEOUT;
    }

    // Send data
    if (send(sock, post_data, strlen(post_data), 0) < 0)
    {
        ESP_LOGE(TAG_WIFI, "Send failed");
        close(sock);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG_WIFI, "Data sent: %s", post_data);
    close(sock);
    return ESP_OK;
}

static void enter_deep_sleep(void)
{
    ESP_LOGI(TAG_PM, "Entering deep sleep for %d seconds", DEEP_SLEEP_TIME_SEC);
    esp_deep_sleep(DEEP_SLEEP_TIME_SEC * 1000000ULL);
}

// Function to handle measurements and data sending
static esp_err_t measure_and_send(adc_oneshot_unit_handle_t adc1_handle)
{
    if (adc1_handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    int32_t adc_sum = 0;
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
        adc_sum += raw_value;
        vTaskDelay(pdMS_TO_TICKS(ADC_SAMPLE_DELAY_MS));
    }

    float raw_value = (float)adc_sum / ADC_SAMPLES;

    // Convert to temperature
    float v_out = (raw_value / ADC_MAX_VALUE) * VREF;
    float resistance = calibrated_resistor * v_out / (VREF - v_out);
    float temperature_kelvin = BETA / (log(resistance / R2) + (BETA / T2));
    float temperature_celsius = temperature_kelvin - KELVIN_TO_CELSIUS;

    ESP_LOGI(TAG_TEMP, "Temperature: %.2f°C (Resistance: %.2f Ω)",
             temperature_celsius, resistance);

    return send_data(temperature_celsius);
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

static void init_watchdog(void)
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
    wifi_quick_connect();

    if (wifi_connected)
    {
        ESP_LOGI(TAG_WIFI, "Connected, taking measurement");
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
        esp_err_t result = measure_and_send(adc1_handle);

        if (result == ESP_OK)
        {
            ESP_LOGI(TAG_PM, "Measurement successful, entering deep sleep");
            esp_wifi_stop();
            adc_oneshot_del_unit(adc1_handle);
            esp_task_wdt_delete(NULL);
            enter_deep_sleep();
        }
        else
        {
            ESP_LOGE(TAG_ADC, "Measurement failed with error: %d", result);
        }
        esp_wifi_stop();
    }
    else
    {
        ESP_LOGE(TAG_WIFI, "WiFi connection failed");
    }
    vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
}

void app_main(void)
{
    // Initialize components
    init_nvs();
    init_watchdog();
    adc_oneshot_unit_handle_t adc1_handle = init_adc();
    init_buttons();

    bool start_measurements = false;

    while (1)
    {
        // Handle calibration button
        if (gpio_get_level(BUTTON_CALIBRATE) == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
            if (gpio_get_level(BUTTON_CALIBRATE) == 0)
            {
                ESP_LOGI(TAG_ADC, "Calibration button pressed");
                calibrate_sensor(adc1_handle);
                while (gpio_get_level(BUTTON_CALIBRATE) == 0)
                {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }

        // Handle start button
        if (gpio_get_level(BUTTON_START) == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
            if (gpio_get_level(BUTTON_START) == 0)
            {
                ESP_LOGI(TAG_ADC, "Start button pressed");
                start_measurements = true;
                while (gpio_get_level(BUTTON_START) == 0)
                {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }

        if (start_measurements)
        {
            ESP_LOGI(TAG_ADC, "Starting measurements");
            handle_measurements(adc1_handle);
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}