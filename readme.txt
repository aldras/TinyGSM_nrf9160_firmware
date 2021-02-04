In order to use this source code install nRF Connect and then install the "Toolcahin Manager"
From the "Toolchain Manager" install v1.4.99-dev1, this should create a folder structure 
in the following location, if you are on MacOS, not sure about other platforms...
/opt/nordic/ncs/v1.4.99-dev1

You need to use the "Toolchain Manager" to enter the command prompt with the correct
environment settings.  This is from the drop down arrow next to the v1.4.99-dev1.
Drop down the menu and choose "Open Terminal" to enter the build environment.

Place the folders in this zip file in the following folder...
/opt/nordic/ncs/v1.4.99-dev1/nrf/applications

Use these commands to build the application...

cd /opt/nordic/ncs/v1.4.99-dev1/nrf/applications/serial_lte_modem_actinius_icarus_115200
west build -b actinius_icarus_ns -p

cd /opt/nordic/ncs/v1.4.99-dev1/nrf/applications/serial_lte_modem_actinius_icarus_921600
west build -b actinius_icarus_ns -p

cd /opt/nordic/ncs/v1.4.99-dev1/nrf/applications/serial_lte_modem_circuitdojo_feather_nrf9160_115200
west build -b circuitdojo_feather_nrf9160ns -p

cd /opt/nordic/ncs/v1.4.99-dev1/nrf/applications/serial_lte_modem_circuitdojo_feather_nrf9160_921600
west build -b circuitdojo_feather_nrf9160ns -p

cd /opt/nordic/ncs/v1.4.99-dev1/nrf/applications/serial_lte_modem_nrf9160dk_115200
west build -b nrf9160dk_nrf9160ns -p
*** DON'T FORGET to change the nRF52/nRF91 switch to nRF91 to program the nRF9160

cd /opt/nordic/ncs/v1.4.99-dev1/nrf/applications/serial_lte_modem_nrf9160dk_921600
west build -b nrf9160dk_nrf9160ns -p
*** DON'T FORGET to change the nRF52/nRF91 switch to nRF91 to program the nRF9160


*** if the nRF9160 baseband needs updating, follow directions in the baseband folder

to program the board controller for the nRF9160DK (the nRF52840)
*** DON'T FORGET to change the nRF52/nRF91 switch to nRF52 to program the nRF52840
nrfjprog -f NRF52 --program nRF9160_DK_board_controller_FW.hex --sectorerase

to program, change to the correct folder above and use this command if using nrfjprog
nrfjprog -f NRF91 --program build/zephyr/merged.hex --sectorerase