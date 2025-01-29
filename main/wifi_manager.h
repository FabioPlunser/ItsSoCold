#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <esp_err.h>
#include <stdbool.h>
#include "esp_task_wdt.h"
#include "power_manager.h"

void wifi_init(void);
esp_err_t wifi_quick_connect(void);
esp_err_t send_data(float temperature);
bool initialize_sntp(void);

extern bool wifi_connected;
extern bool sntp_initialized;

#endif // WIFI_MANAGER_H