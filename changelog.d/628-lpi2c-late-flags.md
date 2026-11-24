### Added

- **The FRDM-MCXN947's I2C test covers a flag that shows after the error flush** (#628): a refused byte keeps its NACK when the bus raises a new flag as its STOP is queued or when its STOP loses arbitration, and a read that fails with bytes in the receive FIFO has them flushed.
