#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdbool.h>
#include "esp_sleep.h"

extern uint32_t esp_reset_count;

bool is_fresh_start(void);
void enter_deep_sleep(void);
void init_watchdog(void);

#endif // POWER_MANAGER_H