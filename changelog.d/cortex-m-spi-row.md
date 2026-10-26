### Added

- **An `SPI=1` row on the MPS2 boards** (row `cortex-m`, #161). Both board
  tables name the shield 0 SPI select as pin 2, the FPGA I/O block's MISC bit 8
  (port 1 bit 8, AN505 4.6 and Table 4-5; AN500 3.7 and Table 3-6), with no
  flags. `SPI=1` defines `TFW_CM_SPI`, which turns `TFW_FEATURE_SPI` on in the
  board profile, and lists SPI in `BOARD_FEATURES_ON`, which links
  `src/caps/bus.c`, `hal_bus.c` and `pl022.c`. `BOARD_ROWS` gains `soft:SPI=1`
  on both boards. The default build is unchanged except for the third pin in
  the table. `test/unit/test_mps2_spi_row.c` checks the row's CAPS bits and an
  `SPI_XFER` on pin 2 against a modelled PL022 in `make host-test`.
