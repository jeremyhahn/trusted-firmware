### Added

- **A test for the ESP32 layer 0 seal's refusals** (#640): `make scripts-test` checks that `boot/hal/esp32/l0_seal.py` refuses a SUIT envelope that is empty, over 320 bytes or names another image, names a suit-emit step that fails with its output, and refuses a board id of 0 or past one byte and an image version of 0 or past 32 bits before any key is loaded, with no ESP-IDF and no cryptography package.
