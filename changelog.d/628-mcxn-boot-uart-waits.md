### Added

- **The FRDM-MCXN947's tfw-boot UART waits are tested** (#628): putc writes DATA only once TDRE is set, and stop resets the unit only once TC is set, not on TDRE alone.
