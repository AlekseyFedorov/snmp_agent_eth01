#include "esp_eth_netif_glue.h"
#include "esp_eth_driver.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "ethernet_app.h"
#include "led_app.h"

#include "nvs.h"

// Значения по умолчанию
char ip_addr_str[16] = "10.149.130.75";
char gw_addr_str[16] = "10.149.130.65";
char netmask_str[16] = "255.255.255.224";

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
  esp_netif_t *eth_netif = (esp_netif_t *)arg;
  if (event_base == ETH_EVENT && event_id == ETHERNET_EVENT_CONNECTED) {
    status_leds_set_network(true);
    esp_netif_dhcpc_stop(eth_netif);
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = esp_ip4addr_aton(ip_addr_str);
    ip_info.gw.addr = esp_ip4addr_aton(gw_addr_str);
    ip_info.netmask.addr = esp_ip4addr_aton(netmask_str);
    esp_netif_set_ip_info(eth_netif, &ip_info);
  } else if (event_base == ETH_EVENT &&
             (event_id == ETHERNET_EVENT_DISCONNECTED ||
              event_id == ETHERNET_EVENT_STOP)) {
    status_leds_set_network(false);
  }
}

static void load_setting(nvs_handle_t handle, const char *key, char *buf, size_t buf_size, bool *dirty)
{
  size_t sz = buf_size;
  if (nvs_get_str(handle, key, buf, &sz) != ESP_OK) {
    nvs_set_str(handle, key, buf);
    *dirty = true;
  }
}

void load_network_settings(void) {
  nvs_handle_t my_handle;
  if (nvs_open("storage", NVS_READWRITE, &my_handle) != ESP_OK) return;

  bool dirty = false;
  load_setting(my_handle, "ip_addr", ip_addr_str, sizeof(ip_addr_str), &dirty);
  load_setting(my_handle, "gw_addr", gw_addr_str, sizeof(gw_addr_str), &dirty);
  load_setting(my_handle, "netmask", netmask_str,  sizeof(netmask_str),  &dirty);

  if (dirty) nvs_commit(my_handle);
  nvs_close(my_handle);
}

esp_err_t ethernet_init_static(void) {
  ESP_ERROR_CHECK(esp_netif_init());

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

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();

  esp32_emac_config.smi_gpio.mdc_num = 23;
  esp32_emac_config.smi_gpio.mdio_num = 18;

  // RMII clock output on GPIO 16 for external LAN8720 oscillator
  esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_OUT;
  esp32_emac_config.clock_config.rmii.clock_gpio = 16;

  esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);

  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.phy_addr = 1;
  phy_config.reset_gpio_num = -1;

  // v6.0.1: use generic PHY driver (works with LAN8720 and all 802.3 PHYs)
  esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_config);

  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t eth_handle = NULL;

  ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

  ESP_ERROR_CHECK(
      esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
  return esp_eth_start(eth_handle);
}
