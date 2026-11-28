### Added

- **The STM32H5 reset cause is tested with the marker's whole range** (#599).
  `test/unit/test_stm32h5_reset.c` decodes a reset request with marker values
  other than 1: each is the firmware's own restart; and takes a latched
  request without the marker: it reads as unknown and RMVF is still written
  (`src/hal/cortex-m/stm32h5/soc_reset.c`). Test only; the driver is
  unchanged.
