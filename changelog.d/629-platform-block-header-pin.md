### Changed

- **The SDK's platform block is held to the bootloader's header** (row `host-sdk`): a Go test reads `include/tfw/boot.h` and checks the platform block layout (magic, version, length, offsets, field lengths) and `EEPROMReservedEnd` against its `TFW_BOOT_EE_*` defines, and checks that `BuildPlatformBlock` writes the baked vector `test/unit/boot/test_boot_platform.c` decodes, so a moved offset or a version bump on either side fails a test.
