### Added

- **`scripts/test_app_symbols.py`, a test of the check app-firmware-check
  runs** (#635). An image linked by the host cc with `--gc-sections` pins
  `scripts/app_symbols.py`: a symbol the link kept is found, one it dropped
  exits 1, a compiler suffix such as `.lto_priv.0` still matches, and a file
  that is not ELF is refused. Hand-built minimal headers cover the 32-bit and
  big-endian images the host cc cannot link. `make scripts-test` runs it.
