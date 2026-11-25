### Added

- **The FRDM-MCXN947's LPI2C timing has a test on its CLKHI 0 refusal** (#628): from 12 MHz, 1.5 MHz is timed with CLKHI 1, and 1.75 MHz and 2 MHz, which the search times, are refused because CLKHI would be 0, with the output left alone.
