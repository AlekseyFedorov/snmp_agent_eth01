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
