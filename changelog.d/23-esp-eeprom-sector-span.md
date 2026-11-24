### Fixed

- **The ESP32 EEPROM's sector-crossing write and unchanged-bytes skip are pinned** (#23): two host tests now check that `tfw_esp_part_write` in `src/hal/esp32/hal_eeprom.c` lands a write crossing a sector boundary in both sectors, each with its own bytes, and that a write of the bytes the flash already holds erases and writes nothing, so dropping the loop's step to the next sector or erasing an unchanged sector (wearing the NOR) fails the ESP EEPROM host suite.
