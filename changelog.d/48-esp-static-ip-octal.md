### Fixed

- **The ESP32 build refuses a static address octet with a leading zero** (#48).
  `mk/arch/esp32/main/CMakeLists.txt` accepted an octet written `010`, which lwIP reads
  as octal and boots as 8, and one written `08`, which lwIP refuses, so the
  board booted with no address. Each octet of `CONFIG_TFW_ETH_IP_ADDR`,
  `CONFIG_TFW_ETH_NETMASK` and `CONFIG_TFW_ETH_GATEWAY` must now be decimal
  0-255 with no leading zero, and `scripts/test_esp32_mk.py` runs the check.
