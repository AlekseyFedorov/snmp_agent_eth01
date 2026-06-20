set(SNMP_READ_COMMUNITY "public" CACHE STRING "SNMP read community")
set(SNMP_WRITE_COMMUNITY "private" CACHE STRING "SNMP write community")

idf_component_register(
    SRCS "main.c" "ethernet_app.c" "sensors_app.c" "led_app.c"
    INCLUDE_DIRS "."
    REQUIRES snmp_lwip esp_netif esp_eth esp_event nvs_flash esp_timer esp_driver_gpio spi_flash
             heap esp_system espressif__ds18b20 espressif__onewire_bus
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    -DSNMP_READ_COMMUNITY="${SNMP_READ_COMMUNITY}"
    -DSNMP_WRITE_COMMUNITY="${SNMP_WRITE_COMMUNITY}"
)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-variable -Wno-unused-function
)
