#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
//#include "esp_flash.h"
#include "esp_flash.h"
//#include "spi_flash_mmap.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"       // Для аптайма
#include "esp_system.h"      // Для памяти
#include "esp_mac.h"

#include "lwip/apps/snmp.h"
#include "lwip/apps/snmp_mib2.h"
#include "lwip/apps/snmp_scalar.h"

#include "ethernet_app.h"
#include "sensors_app.h"

static const char *TAG = "SNMP_MAIN";
static int32_t cached_temp_x10 = 0;

// Данные для MIB2
static u8_t syscontact_storage[64] = "gpbu17672";
static u16_t syscontact_len = 9;
static u8_t syslocation_storage[64] = "GPB Bel";
static u16_t syslocation_len = 7;


// Фоновая задача опроса датчика
void sensor_polling_task(void *pvParameters) {
    while (1) {
        float temp = get_sensor_temperature();
        if (temp > -100.0) {
            cached_temp_x10 = (int32_t)(temp * 10);
            ESP_LOGI(TAG, "Temp updated: %.1f°C", temp); // Теперь LOGI, чтобы видеть в мониторе
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static s16_t get_system_metrics(const struct snmp_scalar_array_node_def *node, void *value) {
    uint32_t *uint_ptr = (uint32_t *)value;
    uint8_t mac[6];

    switch (node->oid) {
        case 1: // Температура
            *(int32_t *)value = cached_temp_x10;
            return sizeof(int32_t);

        case 2: // Свободная память
            *uint_ptr = esp_get_free_heap_size();
            return sizeof(uint32_t);

        case 3: // Uptime
            *uint_ptr = (uint32_t)(esp_timer_get_time() / 10000);
            return sizeof(uint32_t);

        case 4: // Загрузка процессора (упрощенно)
            // В реальных задачах нужен сложный расчет, здесь вернем 
            // имитацию нагрузки на основе idle задач или константу для теста
            *uint_ptr = 100 - (esp_get_free_heap_size() % 10); // Временная заглушка
            return sizeof(int32_t);

        case 5: // Уникальный номер (Base MAC Address)
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
            // Копируем 6 байт MAC-адреса в буфер SNMP
            memcpy(value, mac, 6);
            return 6;

        default:
            return 0;
    }
}

// static s16_t get_system_metrics(const struct snmp_scalar_array_node_def *node, void *value) {
//     uint32_t *uint_ptr = (uint32_t *)value;

//         switch (node->oid) {
//         case 1: // Температура (.1.1.0)
//             *(int32_t *)value = cached_temp_x10;
//             return sizeof(int32_t);

//         case 2: // Свободная память (.1.2.0)
//             *uint_ptr = esp_get_free_heap_size();
//             return sizeof(uint32_t);

//         case 3: // Uptime в сотых долях секунды (.1.3.0)
//             *uint_ptr = (uint32_t)(esp_timer_get_time() / 10000);
//             return sizeof(uint32_t);

//         default:
//             return 0;
//     }
    
//     return 0;

// }
// --- СТРУКТУРА УЗЛОВ ---
// static const struct snmp_scalar_array_node_def sensor_nodes_def[] = {
//     {1, SNMP_ASN1_TYPE_INTEGER,   SNMP_NODE_INSTANCE_READ_ONLY}, // Temp
//     {2, SNMP_ASN1_TYPE_GAUGE32,   SNMP_NODE_INSTANCE_READ_ONLY}, // Heap
//     {3, SNMP_ASN1_TYPE_TIMETICKS, SNMP_NODE_INSTANCE_READ_ONLY}  // Uptime
// };

static const struct snmp_scalar_array_node_def sensor_nodes_def[] = {
    {1, SNMP_ASN1_TYPE_INTEGER,      SNMP_NODE_INSTANCE_READ_ONLY}, // Temp
    {2, SNMP_ASN1_TYPE_GAUGE32,      SNMP_NODE_INSTANCE_READ_ONLY}, // Heap
    {3, SNMP_ASN1_TYPE_TIMETICKS,    SNMP_NODE_INSTANCE_READ_ONLY}, // Uptime
    {4, SNMP_ASN1_TYPE_INTEGER,      SNMP_NODE_INSTANCE_READ_ONLY}, // CPU Load
    {5, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_NODE_INSTANCE_READ_ONLY}  // Device ID (MAC)
};

static const struct snmp_scalar_array_node snmp_sensor_node = 
    SNMP_SCALAR_CREATE_ARRAY_NODE(1, sensor_nodes_def, get_system_metrics, NULL, NULL);

static const u32_t my_base_oid[] = { 1, 3, 6, 1, 4, 1, 9999, 1 };

static const struct snmp_mib sensor_mib = 
    SNMP_MIB_CREATE(my_base_oid, &snmp_sensor_node.node.node);

static const struct snmp_mib *mibs[] = { &mib2, &sensor_mib };

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    uint32_t flash_size;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Hardware Flash Size: %lu MB", flash_size / (1024 * 1024));
    } else {
        ESP_LOGE(TAG, "Failed to get flash size");
    }

    if (ethernet_init_static() == ESP_OK) {
        sensors_init();
        xTaskCreate(sensor_polling_task, "sensor_task", 4096, NULL, 5, NULL);

        syscontact_len = (u16_t)strlen((char*)syscontact_storage);
        syslocation_len = (u16_t)strlen((char*)syslocation_storage);

        snmp_set_community("public");
        snmp_mib2_set_syscontact(syscontact_storage, &syscontact_len, sizeof(syscontact_storage));
        snmp_mib2_set_syslocation(syslocation_storage, &syslocation_len, sizeof(syslocation_storage));
        
        snmp_set_mibs(mibs, LWIP_ARRAYSIZE(mibs));
        snmp_init();

        ESP_LOGI(TAG, "SNMP Agent Started with Metrics (Temp, Heap, Uptime)");
    }
}


// #include <string.h>
// #include "esp_log.h"
// #include "nvs_flash.h"
// #include "esp_event.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

// // SNMP & LwIP
// #include "lwip/apps/snmp.h"
// #include "lwip/apps/snmp_mib2.h"
// #include "lwip/apps/snmp_scalar.h"

// // Ваши модули
// #include "ethernet_app.h"
// #include "sensors_app.h"

// static const char *TAG = "SNMP_MAIN";

// // --- Глобальные данные ---
// static int32_t cached_temp_x10 = 0; // Сюда будем сохранять температуру

// // Данные для системной информации MIB2
// static u8_t syscontact_storage[64] = "ESP32 Admin";
// static u16_t syscontact_len = 11;
// static u8_t syslocation_storage[64] = "Lab Room 1";
// static u16_t syslocation_len = 10;

// // --- 1. Фоновая задача для опроса датчика ---
// // Это критически важно: DS18B20 читается долго (800мс), 
// // поэтому мы делаем это здесь, а не внутри SNMP-запроса.
// void sensor_polling_task(void *pvParameters) {
//     ESP_LOGI(TAG, "Sensor polling task started.");
//     while (1) {
//         float temp = get_sensor_temperature();
//         if (temp > -100.0) { // Проверка на корректность (не ошибка)
//             cached_temp_x10 = (int32_t)(temp * 10);
//             ESP_LOGI(TAG, "Temperature updated: %.1f", temp);
//         } else {
//             ESP_LOGW(TAG, "Sensor read error!");
//         }
//         // Опрашиваем датчик раз в 2 секунды
//         vTaskDelay(pdMS_TO_TICKS(1000));
//     }
// }

// // --- 2. SNMP Callback (Обработчик OID) ---
// static s16_t get_temp_value(const struct snmp_scalar_array_node_def *node, void *value) {
//     // ВАЖНО: записываем значение по указателю value
//     int32_t *int_ptr = (int32_t *)value;
//     *int_ptr = cached_temp_x10;
    
//     // ВАЖНО: возвращаем РАЗМЕР данных в байтах, а не само значение!
//     return sizeof(int32_t); 
// }

// // --- 3. Описание MIB структуры ---

// // Описание узла: ID=1, Тип=INTEGER, Только для чтения
// static const struct snmp_scalar_array_node_def sensor_nodes_def[] = {
//     {1, SNMP_ASN1_TYPE_INTEGER, SNMP_NODE_INSTANCE_READ_ONLY}
// };

// // Создаем скалярный узел (наш .1.1)
// static const struct snmp_scalar_array_node snmp_sensor_node = 
//     SNMP_SCALAR_CREATE_ARRAY_NODE(1, sensor_nodes_def, get_temp_value, NULL, NULL);

// // Базовый OID для нашего MIB: .1.3.6.1.4.1.9999.1
// static const u32_t my_base_oid[] = { 1, 3, 6, 1, 4, 1, 9999, 1 };

// // Регистрация кастомного MIB
// static const struct snmp_mib sensor_mib = 
//     SNMP_MIB_CREATE(my_base_oid, &snmp_sensor_node.node.node);

// // Список всех активных MIB (стандартный MIB2 + наш сенсор)
// static const struct snmp_mib *mibs[] = {
//     &mib2,
//     &sensor_mib
// };

// // --- 4. Основная функция ---
// void app_main(void) {
//     // Инициализация NVS и событий
//     esp_err_t ret = nvs_flash_init();
//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         ret = nvs_flash_init();
//     }
//     ESP_ERROR_CHECK(ret);
//     ESP_ERROR_CHECK(esp_event_loop_create_default());

//     // Инициализация железа
//     if (ethernet_init_static() == ESP_OK) {
//         sensors_init();

//         // Запускаем опрос датчика в отдельном потоке
//         xTaskCreate(sensor_polling_task, "sensor_task", 4096, NULL, 5, NULL);

//         // Настройка SNMP
//         snmp_set_community("public");
        
//         // Заполняем системные поля MIB2 (sysContact, sysLocation)
//         snmp_mib2_set_syscontact(syscontact_storage, &syscontact_len, sizeof(syscontact_storage));
//         snmp_mib2_set_syslocation(syslocation_storage, &syslocation_len, sizeof(syslocation_storage));
        
//         // Регистрируем MIB-ы и запускаем агента
//         snmp_set_mibs(mibs, LWIP_ARRAYSIZE(mibs));
//         snmp_init();

//         ESP_LOGI(TAG, "SNMP Agent Started.");
//         ESP_LOGI(TAG, "Use: snmpget -v 2c -c public 192.168.2.50 .1.3.6.1.4.1.9999.1.1.0");
//     } else {
//         ESP_LOGE(TAG, "Ethernet initialization failed!");
//     }
// }