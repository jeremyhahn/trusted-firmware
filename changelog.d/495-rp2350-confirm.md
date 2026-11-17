### Fixed

- **OTA_CONFIRM on the RP2350 now reaches tfw-boot** (#495). The platform
  block is QSPI flash behind XIP, where a store writes nothing, so the confirm
  request the EEPROM HAL wrote at `TFW_BOOT_CONFIRM_ADDR` never reached the
  sector tfw-boot reads and a trial image could not be confirmed. The write
  is now a read-modify-write of the platform block's first sector through
  tfw-boot's flash helper (`tfw_rp_flash_update`), interrupts masked and the
  masked time credited as for the user sector, then read back through XIP.
  Host-tested over the NOR model, reading the request back from the platform
  sector (`test/unit/test_rp2350_eeprom.c`).
