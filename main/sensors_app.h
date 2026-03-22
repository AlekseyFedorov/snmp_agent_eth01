#ifndef SENSORS_APP_H
#define SENSORS_APP_H

#include "esp_err.h"

// Изменяем на void, чтобы соответствовать .c файлу
void sensors_init(void);
float get_sensor_temperature(void);
int get_water_leak_status(void);

#endif