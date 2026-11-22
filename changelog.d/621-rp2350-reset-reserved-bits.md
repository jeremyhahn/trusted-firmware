### Added

- **A host test pins the RP2350 reset decode's `CHIP_RESET` mask** (row
  `rp2350`, #621). Bit 20, bits 29-31 and the undefined low bits are no source:
  beside one `HAD_` flag they change nothing, and alone they are
  `TFW_RESET_UNKNOWN`, so a widened mask now fails `test_rp2350_reset`.
