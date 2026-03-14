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