### Added

- **app_symbols.py has a test** (row `application-sdk`).
  `scripts/test_app_symbols.py`, run by `make scripts-test`, builds ELF32 and
  ELF64 images byte by byte and pins the reader app-firmware-check relies on:
  a symbol defined in a loaded section is printed (exit 0); a gc-dropped one,
  absent, left in an unloaded section or undefined, is not (exit 1); a
  `.lto_priv.0` suffix matches its name; a non-ELF file is refused (exit 2).
  Where gcc is installed it also links a host image with `--gc-sections` and
  checks the kept function is found and the dropped one is not. The script
  itself is unchanged.
