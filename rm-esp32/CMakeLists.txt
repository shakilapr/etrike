cmake_minimum_required(VERSION 3.16.0)

# Exclude heavy components not used by the e-trike CAN nodes
set(EXCLUDE_COMPONENTS esp_wifi esp_netif lwip mbedtls bt nimble bluedroid mqtt tcpip_adapter mdns wifi_provisioning wpa_supplicant)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rm-esp32)
