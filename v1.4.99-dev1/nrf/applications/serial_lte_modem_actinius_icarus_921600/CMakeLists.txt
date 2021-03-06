#
# Copyright (c) 2019 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

cmake_minimum_required(VERSION 3.13.1)

# This sample runs as a non-secure application on nRF91. Therefore, it
# requires the secure_partition_manager that prepares the required
# peripherals to be available for the application.
#
# Configure the SPM image to enable the peripherals that this sample
# needs.
set(mcuboot_CONF_FILE
  ${CMAKE_CURRENT_LIST_DIR}/slm_mcuboot.conf
  )

set(spm_CONF_FILE
  prj.conf
  ${CMAKE_CURRENT_LIST_DIR}/child_secure_partition_manager.conf
  )

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(serial_lte_modem)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/slm_util.c)
target_sources(app PRIVATE src/slm_at_host.c)
target_sources(app PRIVATE src/slm_at_tcpip.c)
target_sources(app PRIVATE src/slm_at_tcp_proxy.c)
target_sources(app PRIVATE src/slm_at_udp_proxy.c)
target_sources(app PRIVATE src/slm_at_icmp.c)
target_sources(app PRIVATE src/slm_at_fota.c)
target_sources_ifdef(CONFIG_SLM_NATIVE_TLS app PRIVATE src/slm_native_tls.c)
target_sources_ifdef(CONFIG_SLM_NATIVE_TLS app PRIVATE src/slm_at_cmng.c)

add_subdirectory(src/gps)
add_subdirectory(src/ftp_c)
add_subdirectory(src/mqtt_c)
add_subdirectory(src/http_c)

zephyr_include_directories(src)

# added to make compatible with the Actinius Icarus IoT board's bootloader
#set(PM_STATIC_YML_FILE ${CMAKE_CURRENT_SOURCE_DIR}/pm_static.yml)
#list(APPEND mcuboot_OVERLAY_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/mcuboot_overlay-rsa.conf")
