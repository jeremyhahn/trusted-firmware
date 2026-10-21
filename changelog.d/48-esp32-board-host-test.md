### Added

- **The P4's C6 hold runs on the host** (#48, row `esp32-p4`).
  `test_esp_board` links `src/hal/esp32/hal_board.c` against the esp32p4-poe
  profile over a mock gpio driver: the C6 enable is driven low before it is an
  output, pulled down, held, and never driven high; a refused step is
  `TFW_ERR_INTERNAL` and nothing after it runs.
