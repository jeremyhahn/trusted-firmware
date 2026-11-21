### Added

- **A host test pins an AVR reset with EXTRF and PORF both set to
  TFW_RESET_EXTERNAL** (#627), the BORF, EXTRF, PORF priority
  src/hal/avr/hal_reset_decode.h documents; a decoder that let PORF win would
  have SYS_INFO report a reset-pin press as a power-on.
