### Added

- **The FRDM-MCXN947's I2C test covers the clean start and a STOP that never shows** (#628): a flag left from the last transfer is cleared, a byte left in the receive FIFO is not handed to the next read, and a command left in the transmit FIFO does not run ahead of the new START; a STOP that is queued but never shows is a timeout that resets the master and lets the bus go.
