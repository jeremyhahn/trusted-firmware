### Added

- **A test for the ESP32 layer 0 seal's image helpers** (#640): `make scripts-test` checks how `boot/hal/esp32/l0_seal.py` walks an ESP-IDF image, how it grows the zero pad to make room for the trailer, and how it reads `TFW_BOARD_ID`, with no ESP-IDF and no cryptography package.
