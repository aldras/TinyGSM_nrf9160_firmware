set(mcuboot_KERNEL_HEX_NAME zephyr.hex)
set(mcuboot_ZEPHYR_BINARY_DIR /opt/nordic/ncs/v1.4.99-dev1/nrf/applications/serial_lte_modem_nrf9160dk_115200/build/mcuboot/zephyr)
set(mcuboot_KERNEL_ELF_NAME zephyr.elf)
list(APPEND mcuboot_BUILD_BYPRODUCTS /opt/nordic/ncs/v1.4.99-dev1/nrf/applications/serial_lte_modem_nrf9160dk_115200/build/mcuboot/zephyr/zephyr.hex)
list(APPEND mcuboot_BUILD_BYPRODUCTS /opt/nordic/ncs/v1.4.99-dev1/nrf/applications/serial_lte_modem_nrf9160dk_115200/build/mcuboot/zephyr/zephyr.elf)
set(mcuboot_SIGNATURE_KEY_FILE root-rsa-2048.pem)
