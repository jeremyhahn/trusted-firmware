### Added

- **The MCXN94x ADC test covers per-side gain calibration** (#628): `test/unit/test_mcxn94x_adc.c` gives LPADC0's two GCC sides different gain results and checks that each side's GCR is computed from its own, so a HAL that trimmed side B with side A's gain now fails.
