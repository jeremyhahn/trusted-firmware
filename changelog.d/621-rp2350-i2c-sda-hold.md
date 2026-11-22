### Fixed

- **RP2350 I2C init programs the SDA hold** (row `rp2350`). `tfw_hal_i2c_init`
  now writes `IC_SDA_HOLD` while the block is disabled, so SDA is held at least
  300 ns after SCL falls (120 ns at fast-mode plus), as UM10204 table 10 note 2
  requires; it was left at its reset value of one `ic_clk`, 83 ns at 12 MHz.
