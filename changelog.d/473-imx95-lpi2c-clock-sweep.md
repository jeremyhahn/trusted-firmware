### Added

- **A host test pins the i.MX 95 LPI2C clock arithmetic beyond 100 kHz**
  (#473). test_imx95_lpi2c_clock checks 400 kHz and 1 MHz from 24 MHz, the
  step to prescaler 1 when the low count would reach 64 (98 against 99 MHz to
  1 MHz), the smallest cycle accepted (quotient 5) and the refusal at 4, and
  sweeps seven roots against four bus rates against Linux v6.12's
  lpi2c_imx_config().
