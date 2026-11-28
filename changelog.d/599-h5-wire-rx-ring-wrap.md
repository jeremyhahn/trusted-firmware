### Added

- **The STM32H5 tfw-wire TCP receive ring is tested past its first wrap**
  (#599). `test/unit/test_h5_eth_wire.c` reads bytes out of a full ring and
  pushes again: exactly the room read is taken back; and a push that crosses
  the ring's end is read back byte for byte in order
  (`src/hal/cortex-m/stm32h5/hal_eth_wire.c`). Test only; the driver is
  unchanged.
