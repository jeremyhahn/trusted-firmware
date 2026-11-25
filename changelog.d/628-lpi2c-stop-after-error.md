### Added

- **The FRDM-MCXN947's I2C test covers a STOP that fails after the last byte or after a refusal** (#628): a STOP that cannot be queued after the last byte is a timeout, waited for once, that resets the master and lets the bus go; a refused byte keeps its NACK when its STOP then cannot be queued or never shows.
