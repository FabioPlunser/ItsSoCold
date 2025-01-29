#ifndef SENSOR_H
#define SENSOR_H

#include "esp_adc/adc_oneshot.h"

void calibrate_sensor(adc_oneshot_unit_handle_t adc1_handle);
esp_err_t measure_and_send(adc_oneshot_unit_handle_t adc1_handle);

#endif // SENSOR_H