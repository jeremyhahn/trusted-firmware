### Fixed

- **The AVR EEPROM HAL refuses a read or write past TFW_EEPROM_SIZE with
  TFW_ERR_RANGE** (#640) and touches nothing, as the ESP32, RP2350 and
  MCXN947 ports already do. It returned TFW_OK, and on the part the address
  wrapped onto tfw-boot's platform block at 0.
