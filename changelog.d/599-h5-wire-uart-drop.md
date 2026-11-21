### Added

- **A host test pins how the STM32H5 TCP wire resets the transport** (#599): a TCP connect or disconnect while the UART is the current wire raises no reset, so a half-received serial frame survives it, and a TCP peer dropped while TCP is current raises the reset once, read back as 0 the second time.
