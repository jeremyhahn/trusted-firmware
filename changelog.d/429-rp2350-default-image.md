### Changed

- **The Pico 2 W's default image carries its peripherals** (row `rp2350`,
  ruling 697). With the AVR gate (#7) lifted for the RP2350, `rp2350-pico2w`
  builds with `I2C ?= 1` and `PERIPH ?= 1`: the default image links the I2C0
  master, the converter, PWM, the tone and the flash EEPROM with tfw-boot's
  flash helper. `make flash` no longer refuses the I2C and peripheral rows
  (`CM_FLASH_GATED_WHY` is gone from the profile). `BOARD_ROWS` becomes
  `soft:I2C=0 soft:PERIPH=0`, so size-check still measures the images without
  them.
