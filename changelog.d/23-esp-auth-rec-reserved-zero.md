### Fixed

- **The ESP32 activate record's reserved bytes are pinned to zero** (#23): a host test now checks that `tfw_stage_authorize` in `src/hal/esp32/hal_ota.c` writes bytes 5-7 of the OTA_ACTIVATE record as zero, as `include/tfw/boot.h` gives them, so removing the record's clear (which leaves the application's stack in the `tfw_auth` partition) fails the OTA host suite.
