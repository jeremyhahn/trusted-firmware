### Added

- **A host test that I2C_XFER hands the HAL the host's write bytes, their
  length and the read length** (#627). test_bus_optiga's I2C HAL records
  what bus.c passes it: a five-byte write with a three-byte read reaches the
  bus unchanged, and a write-only transfer answers nothing.
