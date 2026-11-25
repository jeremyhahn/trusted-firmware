### Added

- **The nRF54L15 SAADC clamp is tested at its edges** (row #628). A host test
  pins `tfw_nrf54l_saadc_raw` at -1, 1, 4094, 4096, `INT16_MIN` and
  `INT16_MAX`, so a clamp that lets -1 through as 65535 or 4096 through as a
  13-bit value fails the suite.
