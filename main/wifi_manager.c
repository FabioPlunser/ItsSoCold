#include "wifi_manager.h"
#include "wifi_config.h"
#include "config.h"
#include "rtc_store.h"
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_netif.h>
#include <sys/socket.h>

bool wifi_connected = false;
bool sntp_initialized = false;

void wifi_event_handler(void *arg, esp_event_base_t event_base,
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
    }
}

void wifi_init(void)
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
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH,
            .pmf_cfg = {
                .capable = true,
                .required = false},
            .scan_method = WIFI_FAST_SCAN}};

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    ESP_ERROR_CHECK(esp_wifi_start());
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_ERROR_CHECK(esp_wifi_connect());

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
        memcpy(&rtc_store.data.wifi_config, &wifi_config, sizeof(wifi_config_t));
        rtc_store.data.boot_count++;
        ESP_LOGI(TAG_WIFI, "WiFi connected successfully, boot_count: %d", rtc_store.data.boot_count);
    }
}

esp_err_t wifi_quick_connect(void)
{
    if (!wifi_connected)
    {
        wifi_init();
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait for connection
    int retry = 0;
    while (!wifi_connected && retry < WIFI_CONNECT_TIMEOUT_MS)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
    }

    return wifi_connected ? ESP_OK : ESP_FAIL;
}

esp_err_t send_data(float temperature)
{
    if (!wifi_connected)
    {
        ESP_LOGE(TAG_WIFI, "WiFi not connected");
        wifi_connected = false;
        return ESP_ERR_WIFI_NOT_CONNECT;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        return ESP_FAIL;
    }

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

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

    ESP_LOGI(TAG_WIFI, "Sending data: %s", post_data);

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(22504),
        .sin_addr.s_addr = inet_addr(SERVER_IP_ADDR)};

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(sock);
        return ESP_ERR_TIMEOUT;
    }

    int sent = send(sock, post_data, strlen(post_data), 0);
    close(sock);

    return (sent > 0) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

bool initialize_sntp(void)
{
    if (!sntp_initialized)
    {
        ESP_LOGI(TAG_SNTP, "Initializing SNTP");
        esp_sntp_stop();
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, SNTP_SERVER);
        esp_sntp_init();
        sntp_initialized = true;
    }

    // Wait for time to be set
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 15;

    while (timeinfo.tm_year < (2024 - 1900) && retry < retry_count)
    {
        ESP_LOGI(TAG_SNTP, "Waiting for system time to be set... (%d/%d)", retry + 1, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
        retry++;
    }

    if (timeinfo.tm_year >= (2024 - 1900))
    {
        ESP_LOGI(TAG_SNTP, "Time synchronized successfully");
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();
        return true;
    }

    ESP_LOGE(TAG_SNTP, "Failed to get time from SNTP server");
    return false;
}
