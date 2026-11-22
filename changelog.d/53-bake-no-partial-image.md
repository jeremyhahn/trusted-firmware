### Fixed

- `tfwprov --bake-eeprom` removes the half-written Intel HEX image at `--out` when the write or close fails, where it left a truncated file with no end record that `avrdude -U eeprom:w:<out>:i` would still accept (#53).
