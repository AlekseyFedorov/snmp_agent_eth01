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

  esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
  esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);

  // Сначала загружаем настройки из памяти
  load_network_settings();

  // Настраиваем LwIP
  // esp_netif_ip_info_t ip_info;

  // Регистрируем обработчик для установки статического IP при подключении кабеля
  esp_event_handler_instance_t instance_connected;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(ETH_EVENT,
                                                      ETHERNET_EVENT_CONNECTED,
                                                      &eth_event_handler,
                                                      eth_netif,
                                                      &instance_connected));
  //    esp_netif_set_ip_info(eth_netif, &ip_info);

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