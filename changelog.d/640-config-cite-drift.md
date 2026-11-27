### Fixed

- **The docs' line cites point at their symbols again after the 0x160 move** (#640): the cites to `include/tfw/app.h`, `include/tfw/boot.h`, `include/tfw/config.h` and `src/core/app_runner.c` name the lines that hold what they describe, and `include/tfw/app.h` pins `TFW_APP_CONFIG_BASE` at 0x160 at compile time.
