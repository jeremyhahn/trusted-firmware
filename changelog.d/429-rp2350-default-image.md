### Changed

- **The Pico 2 W's default image carries its peripherals** (row `rp2350`,
  ruling 697). With the AVR gate (#7) lifted for the RP2350, `rp2350-pico2w`
  builds with `I2C ?= 1` and `PERIPH ?= 1`: the default image links the I2C0
  master, the converter, PWM, the tone and the flash EEPROM with tfw-boot's
  flash helper. `make flash` no longer refuses the I2C and peripheral rows
  (`CM_FLASH_GATED_WHY` is gone from the profile). `BOARD_ROWS` becomes
  `soft:I2C=0 soft:PERIPH=0`, so size-check still measures the images without
  them. `make host-test` runs the default row's board descriptor test
  (`test_board_rp2350-pico2w_default`), which board-sweep does not build.

### Fixed

- **picotool names the device last** (row `rp2350`). picotool 2.1.1 refuses
  `--ser <serial>` before the file and the command's options, so
  `make board-record`, `make flash` and `make boot-flash` failed on the
  Pico 2 W. mk/arch/cortex-m.mk's picotool save and load now put
  `--ser $(CM_PROBE_SERIAL) $(CM_PROBE_ARGS)` after them.
