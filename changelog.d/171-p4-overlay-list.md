### Changed

- **`ESP_SDKCONFIG_EXTRA` takes a list** (#171, #48). Each file is its own
  `SDKCONFIG_DEFAULTS` entry, in the order given, after the board defaults,
  so the HIL-0 overlay and an out-of-tree static-address overlay build
  together. One file builds as before; a file that does not exist stops
  `make esp32-build` with its name. `make esp32-overlay-selftest`, in the
  security stage, dry-runs the list without ESP-IDF.
