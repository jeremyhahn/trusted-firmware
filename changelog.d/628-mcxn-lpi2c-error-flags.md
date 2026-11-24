### Added

- **The FRDM-MCXN947's I2C test covers every LPI2C error flag** (#628): a lost arbitration, a pin low timeout and a FIFO error on a write each return their own status (busy, timeout, internal), not success, and end with one STOP, the bus let go and no flag left.
