#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "onewire_bus.h"
#include "ds18b20.h"
#include "sensors_app.h"

static const char *TAG = "SENSORS";
#define ONEWIRE_BUS_GPIO    32 

static ds18b20_device_handle_t ds18b20_dev = NULL;

void sensors_init(void) {
    onewire_bus_handle_t bus = NULL;
    onewire_bus_config_t bus_config = { .bus_gpio_num = ONEWIRE_BUS_GPIO };
    onewire_bus_rmt_config_t rmt_config = { .max_rx_bytes = 10 };

    // 1. Создаем шину RMT
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus));

    // 2. Инициализируем итератор поиска
    onewire_device_iter_handle_t iter = NULL;
    ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
    onewire_device_t next_dev;

    // 3. Ищем первое устройство и создаем дескриптор DS18B20
    if (onewire_device_iter_get_next(iter, &next_dev) == ESP_OK) {
        ds18b20_config_t ds_cfg = {}; 
        
        // Используем точное имя из вашего grep
        if (ds18b20_new_device_from_enumeration(&next_dev, &ds_cfg, &ds18b20_dev) == ESP_OK) {
            ESP_LOGI(TAG, "DS18B20 initialized on GPIO %d", ONEWIRE_BUS_GPIO);
        } else {
            ESP_LOGE(TAG, "Failed to create DS18B20 handle");
        }
    } else {
        ESP_LOGW(TAG, "No DS18B20 found!");
    }
    onewire_del_device_iter(iter);
}

float get_sensor_temperature(void) {
    float temp = -127.0;
    if (ds18b20_dev) {
        if (ds18b20_trigger_temperature_conversion(ds18b20_dev) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(800)); // Ждем завершения преобразования
            ds18b20_get_temperature(ds18b20_dev, &temp);
        }
    }
    return temp;
}