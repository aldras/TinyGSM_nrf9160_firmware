set(spm_KERNEL_HEX_NAME zephyr.hex)
set(spm_ZEPHYR_BINARY_DIR /opt/nordic/ncs/v1.4.99-dev1/nrf/applications/serial_lte_modem_actinius_icarus_921600/build/spm/zephyr)
set(spm_KERNEL_ELF_NAME zephyr.elf)
list(APPEND spm_BUILD_BYPRODUCTS /opt/nordic/ncs/v1.4.99-dev1/nrf/applications/serial_lte_modem_actinius_icarus_921600/build/spm/zephyr/zephyr.hex)
list(APPEND spm_BUILD_BYPRODUCTS /opt/nordic/ncs/v1.4.99-dev1/nrf/applications/serial_lte_modem_actinius_icarus_921600/build/spm/zephyr/zephyr.elf)
list(APPEND spm_BUILD_BYPRODUCTS
    /opt/nordic/ncs/v1.4.99-dev1/nrf/applications/serial_lte_modem_actinius_icarus_921600/build/spm/libspmsecureentries.a)
