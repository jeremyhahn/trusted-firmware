### Added

- **RP2350 I2C arbitration-lost test** (row `rp2350`). The I2C0 register model
  can now lose the bus to another master mid-write (`IC_TX_ABRT_SOURCE` bit 12),
  and a host test pins that `tfw_hal_i2c_transfer` reports it as `TFW_ERR_BUSY`.
