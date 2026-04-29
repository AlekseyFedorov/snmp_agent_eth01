set(LWIP_PATH "$ENV{IDF_PATH}/components/lwip/lwip")
file(GLOB SNMP_SOURCES 
    "${LWIP_PATH}/src/apps/snmp/*.c"
    "${LWIP_PATH}/src/apps/snmp/snmp_mib2/*.c"
)

set(SNMP_READ_COMMUNITY "public" CACHE STRING "SNMP read community")
set(SNMP_WRITE_COMMUNITY "private" CACHE STRING "SNMP write community")

idf_component_register(
    SRCS "main.c" "ethernet_app.c" "sensors_app.c" ${SNMP_SOURCES}
    INCLUDE_DIRS "."
    REQUIRES lwip esp_netif esp_eth esp_event nvs_flash esp_timer esp_driver_gpio spi_flash
             heap espressif__ds18b20 espressif__onewire_bus
            #  esp_driver_gpio esp_driver_rmt
)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_source_files_properties(${SNMP_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wno-dangling-pointer")
endif()

target_compile_definitions(${COMPONENT_LIB} PUBLIC 
    -DLWIP_SNMP=1 -DSNMP_LWIP=1 -DSNMP_USE_RAW=1 -DLWIP_SNMP_V2C=1 -DMIB2_STATS=1 -DLWIP_STATS=1 
    SNMP_READ_COMMUNITY="${SNMP_READ_COMMUNITY}"
    SNMP_WRITE_COMMUNITY="${SNMP_WRITE_COMMUNITY}"
    # -DSNMP_MAX_VALUE_SIZE=64
)
target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wno-unused-variable -Wno-unused-function
)
