### Added

- **The FRDM-MCXN947 tone divider's rounding has a host test** (#628): a remainder of exactly half rounds up (12 Hz clock, 4 Hz tone gives two counts), just under it is refused, and a 0xFFFFFFFF clock gives match 0x7FFFFFFF without the `clk + hz` wrap.
