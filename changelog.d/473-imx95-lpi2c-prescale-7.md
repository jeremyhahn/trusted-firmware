### Added

- **A host test pins the i.MX 95 LPI2C clock arithmetic at its last
  prescaler** (#473). test_imx95_lpi2c_clock checks that 50 kHz from 400 MHz
  takes prescaler 7 (MCCR0 0x0A141427), and that 20 kHz from 400 MHz, which
  no prescaler fits, is refused with TFW_ERR_RANGE and leaves MCFGR1 and
  MCCR0 unwritten.
