### Added

- **An SPI master on the MPS2 boards** (row `cortex-m`, #161).
  `src/hal/cortex-m/mps2/hal_bus.c` drives the shield 0 SPI, an Arm PrimeCell
  SSP (PL022, Arm DDI 0194H), through `pl022.c`, the only file that touches its
  registers. The select is the FPGA I/O block's MISC bit 8, not the PL022's own
  output (AN505 4.6 and Table 4-5; AN500 3.7 and Table 3-6). The base is
  `0x4020A000` on mps2-an505 (AN505 Table 3-7) and `0x40026000` on mps2-an500
  (AN500 Table 3-1). `SPI_CONFIG`'s mode and divisor map onto SSPCR0, and
  least-significant-first reverses each byte in software. A select other than
  port 1 bit 8 is `TFW_ERR_BAD_PIN`; a stopped port releases the select and
  returns `TFW_ERR_TIMEOUT`. `TFW_FEATURE_SPI` is on in the `SPI=1` rows
  below. `test/unit/test_mps2_bus.c` runs the master against a
  modelled PL022 in `make host-test`.
