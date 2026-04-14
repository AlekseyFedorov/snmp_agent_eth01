# Описание проекта SNMP Agent

```
1. Структура директорий.


snmp_agent_eth01/
├── main/
│   ├── CMakeLists.txt      # Сборочный файл для компонентов приложения
│   ├── main.c              # Главный файл приложения (логика SNMP и инициализация)
│   ├── ethernet_app.c      # Настройки Ethernet/сетевого интерфейса
│   ├── ethernet_app.h      # Заголовочный файл для ethernet_app.c
│   ├── sensors_app.c       # Логика работы с датчиками (температура, протечка)
│   ├── sensors_app.h       # Заголовочный файл для датчиков
│   └── idf_component.yml   # Зависимости компонента (если есть)
├── managed_components/     # Сторонние компоненты, загруженные менеджером пакетов ESP-IDF
├── CMakeLists.txt          # Главный сборочный файл проекта ESP-IDF
├── README.md               # Документация проекта с инструкциями
├── sdkconfig               # Файл конфигурации проекта (создается через menuconfig)
└── .gitignore              # Исключения для систем контроля версий
```

## 2. Содержимое файлов

### Файлы в директории `main/`

Содержимое файла main/CMakeLists.txt

set(LWIP_PATH "$ENV{IDF_PATH}/components/lwip/lwip")
file(GLOB SNMP_SOURCES 
    "${LWIP_PATH}/src/apps/snmp/*.c"
    "${LWIP_PATH}/src/apps/snmp/snmp_mib2/*.c"
)

idf_component_register(
    SRCS "main.c" "ethernet_app.c" "sensors_app.c" ${SNMP_SOURCES}
    INCLUDE_DIRS "."
    REQUIRES lwip esp_netif esp_eth esp_event nvs_flash esp_timer esp_driver_gpio spi_flash
             espressif__ds18b20 espressif__onewire_bus
            #  esp_driver_gpio esp_driver_rmt
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC 
    -DLWIP_SNMP=1 -DSNMP_LWIP=1 -DSNMP_USE_RAW=1 -DLWIP_SNMP_V2C=1 -DMIB2_STATS=1 -DLWIP_STATS=1 
    # -DSNMP_MAX_VALUE_SIZE=64
)
target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wno-unused-variable -Wno-unused-function -Wno-dangling-pointer
)


Содержимое файла main/idf_component.yml

dependencies:
  espressif/ds18b20: "*"
  espressif/onewire_bus: "*"
  idf: ">=5.0"



Содержимое файлов main/main.c

#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "nvs.h"

#include "lwip/apps/snmp.h"
#include "lwip/apps/snmp_mib2.h"
#include "lwip/apps/snmp_scalar.h"
#include "lwip/ip4_addr.h"

#include "ethernet_app.h"
#include "sensors_app.h"

#define RESET_BUTTON_PIN 0

static const char *TAG = "SNMP_MAIN";

static int32_t cached_temp_x10 = 0;

/* MIB2 system data */
static u8_t syscontact_storage[64] = "gpbu17672";
static u16_t syscontact_len = 9;

static u8_t syslocation_storage[64] = "GPB Bel";
static u16_t syslocation_len = 7;

/* ============================ */
/* SENSOR POLLING TASK          */
/* ============================ */
void sensor_polling_task(void *pvParameters)
{
    while (1)
    {
        float temp = get_sensor_temperature();
        if (temp > -100.0)
        {
            cached_temp_x10 = (int32_t)(temp * 10);
            ESP_LOGI(TAG, "Temp updated: %.1fC", temp);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ============================ */
/* FACTORY RESET TASK           */
/* ============================ */
void factory_reset_task(void *pvParameter)
{
    gpio_reset_pin(RESET_BUTTON_PIN);
    gpio_set_direction(RESET_BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(RESET_BUTTON_PIN, GPIO_PULLUP_ONLY);

    int press_counter = 0;

    while (1)
    {
        if (gpio_get_level(RESET_BUTTON_PIN) == 0)
        {
            press_counter++;

            if (press_counter >= 50)
            {
                ESP_LOGW(TAG, "FACTORY RESET TRIGGERED");

                nvs_handle_t handle;
                if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK)
                {
                    nvs_erase_all(handle);
                    nvs_commit(handle);
                    nvs_close(handle);
                }

                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_restart();
            }
        }
        else
        {
            press_counter = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ============================ */
/* SNMP READ CALLBACK           */
/* ============================ */
static s16_t get_system_metrics(const struct snmp_scalar_array_node_def *node, void *value)
{
    uint32_t *uint_ptr = (uint32_t *)value;
    uint8_t mac[6];

    switch (node->oid)
    {
        case 1:
            *(int32_t *)value = cached_temp_x10;
            return sizeof(int32_t);
        case 2:
            *uint_ptr = esp_get_free_heap_size();
            return sizeof(uint32_t);
        case 3:
            *uint_ptr = (uint32_t)(esp_timer_get_time() / 10000);
            return sizeof(uint32_t);
        case 4:
            *uint_ptr = 100 - (esp_get_free_heap_size() % 10);
            return sizeof(uint32_t);
        case 5:
            esp_read_mac(mac, ESP_MAC_ETH);
            memcpy(value, mac, 6);
            return 6;
        case 6:
            *(int32_t *)value = get_water_leak_status();
            return sizeof(int32_t);
        case 10:
            memcpy(value, ip_addr_str, strlen(ip_addr_str));
            return strlen(ip_addr_str);
        case 11:
            memcpy(value, gw_addr_str, strlen(gw_addr_str));
            return strlen(gw_addr_str);
        case 12:
            memcpy(value, netmask_str, strlen(netmask_str));
            return strlen(netmask_str);
    }
    return 0;
}

/* ============================ */
/* SNMP WRITE CALLBACKS         */
/* ============================ */

// Функция перезагрузки по таймеру
static void reboot_timer_callback(void* arg) {
    esp_restart();
}

// ФАЗА 1: Тестирование валидности данных (set_test)
static snmp_err_t test_system_config(const struct snmp_scalar_array_node_def *node, u16_t len, void *value)
{
    char buffer[16];
    if (len >= sizeof(buffer)) return SNMP_ERR_WRONGLENGTH;

    memcpy(buffer, value, len);
    buffer[len] = 0;

    ip4_addr_t test_ip; // Изменено на ip4_addr_t
    if (!ip4addr_aton(buffer, &test_ip)) return SNMP_ERR_WRONGVALUE; // Изменено на ip4addr_aton

    return SNMP_ERR_NOERROR;
}

// ФАЗА 2: Фактическое сохранение данных (set_value)
static snmp_err_t set_system_config(const struct snmp_scalar_array_node_def *node, u16_t len, void *value)
{
    char buffer[16];
    memcpy(buffer, value, len);
    buffer[len] = 0;

    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) != ESP_OK) return SNMP_ERR_GENERROR;

    switch (node->oid)
    {
        case 10:
            strcpy(ip_addr_str, buffer);
            nvs_set_str(handle, "ip_addr", buffer);
            break;
        case 11:
            strcpy(gw_addr_str, buffer);
            nvs_set_str(handle, "gw_addr", buffer);
            break;
        case 12:
            strcpy(netmask_str, buffer);
            nvs_set_str(handle, "netmask", buffer);
            break;
        default:
            nvs_close(handle);
            return SNMP_ERR_NOTWRITABLE;
    }

    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGW(TAG, "Network config changed via SNMP, rebooting in 1s...");

    // Асинхронный запуск перезагрузки
    esp_timer_create_args_t reboot_args = { .callback = &reboot_timer_callback, .name = "reboot" };
    esp_timer_handle_t timer;
    esp_timer_create(&reboot_args, &timer);
    esp_timer_start_once(timer, 1000000); 

    return SNMP_ERR_NOERROR;
}

/* ============================ */
/* SNMP NODE STRUCTURE          */
/* ============================ */

static const struct snmp_scalar_array_node_def sensor_nodes_def[] =
{
    {1, SNMP_ASN1_TYPE_INTEGER,      SNMP_NODE_INSTANCE_READ_ONLY},
    {2, SNMP_ASN1_TYPE_GAUGE32,      SNMP_NODE_INSTANCE_READ_ONLY},
    {3, SNMP_ASN1_TYPE_TIMETICKS,    SNMP_NODE_INSTANCE_READ_ONLY},
    {4, SNMP_ASN1_TYPE_INTEGER,      SNMP_NODE_INSTANCE_READ_ONLY},
    {5, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_NODE_INSTANCE_READ_ONLY},
    {6, SNMP_ASN1_TYPE_INTEGER,      SNMP_NODE_INSTANCE_READ_ONLY},

    {10, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_NODE_INSTANCE_READ_WRITE},
    {11, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_NODE_INSTANCE_READ_WRITE},
    {12, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_NODE_INSTANCE_READ_WRITE}
};

static const struct snmp_scalar_array_node snmp_sensor_node =
    SNMP_SCALAR_CREATE_ARRAY_NODE(
        1,
        sensor_nodes_def,
        get_system_metrics,
        test_system_config, // ФАЗА 1
        set_system_config   // ФАЗА 2
    );

static const u32_t my_base_oid[] = {1,3,6,1,4,1,9999,1};

static const struct snmp_mib sensor_mib =
    SNMP_MIB_CREATE(my_base_oid, &snmp_sensor_node.node.node);

static const struct snmp_mib *mibs[] =
{
    &mib2,
    &sensor_mib
};

/* ============================ */
/* MAIN                         */
/* ============================ */

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    xTaskCreate(factory_reset_task, "reset_task", 2048, NULL, 5, NULL);

    if (ethernet_init_static() == ESP_OK)
    {
        sensors_init();

        xTaskCreate(sensor_polling_task, "sensor_task", 4096, NULL, 5, NULL);

        syscontact_len = strlen((char*)syscontact_storage);
        syslocation_len = strlen((char*)syslocation_storage);

        snmp_set_community("public");
        snmp_set_community_write("private"); // Исправлено имя функции

        snmp_mib2_set_syscontact(syscontact_storage, &syscontact_len, sizeof(syscontact_storage));
        snmp_mib2_set_syslocation(syslocation_storage, &syslocation_len, sizeof(syslocation_storage));

        snmp_set_mibs(mibs, LWIP_ARRAYSIZE(mibs));
        snmp_init();

        ESP_LOGI(TAG, "SNMP Agent started");
    }
}


Содержимое файлов main/ethernet_app.h

#ifndef ETHERNET_APP_H
#define ETHERNET_APP_H

#include "esp_err.h"

// Экспортируем переменные для использования в main.c
extern char ip_addr_str[16];
extern char gw_addr_str[16];
extern char netmask_str[16];

void load_network_settings(void);
esp_err_t ethernet_init_static(void);

#endif


Содержимое файлов main/ethernet_app.c

#include "esp_eth_netif_glue.h"
#include "esp_eth_driver.h"
#include "esp_event.h"
#include "esp_netif.h"
// #include "esp_log.h"
#include "driver/gpio.h"
#include "ethernet_app.h"

#include "nvs.h"

static const char *TAG = "ETH_WT32";

// Значения по умолчанию
char ip_addr_str[16] = "192.168.2.50";
char gw_addr_str[16] = "192.168.2.1";
char netmask_str[16] = "255.255.255.0";

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
  esp_netif_t *eth_netif = (esp_netif_t *)arg;
  if (event_base == ETH_EVENT && event_id == ETHERNET_EVENT_CONNECTED) {
    esp_netif_dhcpc_stop(eth_netif);
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = esp_ip4addr_aton(ip_addr_str);
    ip_info.gw.addr = esp_ip4addr_aton(gw_addr_str);
    ip_info.netmask.addr = esp_ip4addr_aton(netmask_str);
    esp_netif_set_ip_info(eth_netif, &ip_info);
  }
}

void load_network_settings() {
  nvs_handle_t my_handle;
  // Открываем NVS
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err != ESP_OK)
    return;

  size_t required_size;

  // Читаем IP
  if (nvs_get_str(my_handle, "ip_addr", NULL, &required_size) == ESP_OK &&
      required_size <= 16) {
    nvs_get_str(my_handle, "ip_addr", ip_addr_str, &required_size);
  } else {
    nvs_set_str(my_handle, "ip_addr", ip_addr_str); // Сохраняем дефолт
  }

  // Читаем Шлюз (Gateway)
  if (nvs_get_str(my_handle, "gw_addr", NULL, &required_size) == ESP_OK &&
      required_size <= 16) {
    nvs_get_str(my_handle, "gw_addr", gw_addr_str, &required_size);
  } else {
    nvs_set_str(my_handle, "gw_addr", gw_addr_str);
  }

  // Читаем Маску
  if (nvs_get_str(my_handle, "netmask", NULL, &required_size) == ESP_OK &&
      required_size <= 16) {
    nvs_get_str(my_handle, "netmask", netmask_str, &required_size);
  } else {
    nvs_set_str(my_handle, "netmask", netmask_str);
  }

  nvs_commit(my_handle);
  nvs_close(my_handle);
}

esp_err_t ethernet_init_static(void) {
  ESP_ERROR_CHECK(esp_netif_init());

  esp_err_t ret = esp_event_loop_create_default();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(ret);
  }

  esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
  esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);

  // Сначала загружаем настройки из памяти
  load_network_settings();

  // Регистрируем обработчик для установки статического IP при подключении кабеля
  esp_event_handler_instance_t instance_connected;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(ETH_EVENT,
                                                      ETHERNET_EVENT_CONNECTED,
                                                      &eth_event_handler,
                                                      eth_netif,
                                                      &instance_connected));

  // внешний осциллятор через GPIO 16
  gpio_reset_pin(GPIO_NUM_16);
  gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_16, 1);
  vTaskDelay(pdMS_TO_TICKS(100));

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();

  esp32_emac_config.smi_gpio.mdc_num = 23;
  esp32_emac_config.smi_gpio.mdio_num = 18;

  esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);

  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.phy_addr = 1;
  phy_config.reset_gpio_num = -1;

  esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);

  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t eth_handle = NULL;

  ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

  ESP_ERROR_CHECK(
      esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
  return esp_eth_start(eth_handle);
}


Содержимое файлов main/sensors_app.h

#ifndef SENSORS_APP_H
#define SENSORS_APP_H

#include "esp_err.h"

// Изменяем на void, чтобы соответствовать .c файлу
void sensors_init(void);
float get_sensor_temperature(void);
int get_water_leak_status(void);

#endif


Содержимое файла main/sensors_app.c

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "onewire_bus.h"
#include "ds18b20.h"
#include "sensors_app.h"
#include "driver/gpio.h"

static const char *TAG = "SENSORS";
#define ONEWIRE_BUS_GPIO    32 
#define WATER_LEAK_GPIO     33

static ds18b20_device_handle_t ds18b20_dev = NULL;

void sensors_init(void) {
    // Инициализация датчика протечки H2O-Контакт
    gpio_reset_pin(WATER_LEAK_GPIO);
    gpio_set_direction(WATER_LEAK_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(WATER_LEAK_GPIO, GPIO_PULLUP_ONLY);

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

int get_water_leak_status(void) {
    // Если датчик замкнут (протечка), пин притянется к земле = 0.
    // Если сухо = 1 (благодаря PULLUP).
    // Возвращаем 1 в случае утечки (логический 1 = тревога).
    return (gpio_get_level(WATER_LEAK_GPIO) == 0) ? 1 : 0;
}


Содержимое файла в корне проекта CMakeLists.txt

cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)



3. Краткое описание проекта (Что делает и какую задачу решает)

Что делает проект:
Проект представляет собой прошивку для микроконтроллера ESP32 (с использованием проводного подключения Ethernet), которая превращает устройство в полноценный управляемый **SNMP-агент**. Основные функции:
*   **Сбор аппаратных метрик:** С заданной периодичностью опрашивает датчик температуры (DS18B20 по 1-Wire) и датчик протечки (упоминается "H2O-Контакт").
*   **Мониторинг самой системы:** Отслеживает доступную свободную память, MAC-адрес устройства и общее время работы (uptime).
*   **Удаленное управление настройками сети:** Позволяет менять IP-адрес, шлюз и маску подсети агента по сети с помощью команд `SNMP SET`. Новые данные сразу сохраняются в энергонезависимую память микроконтроллера (NVS), после чего происходит автоматическая перезагрузка для применения настроек.
*   **Аварийный сброс:** Включает механизм сброса к заводским настройкам путем удерживания физической кнопки (PIN 0).

**Какую задачу решает:**
Проект решает важную задачу **удаленного аппаратного или климатического мониторинга серверных помещений, складов или удаленных узлов с помощью стандартизированного протокола**.
Вместо того чтобы изобретать собственные API/протоколы для передачи данных о протечке или температуре, автор реализовал стандартный SNMP (Simple Network Management Protocol). Это позволяет легко подключить устройство к любой существующей системе мониторинга IT-инфраструктуры (например, Zabbix, PRTG, Prometheus+SNMP Exporter, Cacti). Системные администраторы смогут опрашивать состояние температуры, получать тревоги о воде на полу и удалённо менять сетевые реквизиты датчика так же, как они это делают с промышленными сетевыми коммутаторами или серверным оборудованием.
