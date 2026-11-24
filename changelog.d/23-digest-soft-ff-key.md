### Fixed

- **The ESP32 p256-soft erased-slot check is pinned to all 64 bytes** (#23): a host test now verifies an image against a provisioned key whose last byte is 0xFF (private scalar 333), so a `tfw_crypto_verify_image` in `boot/hal/esp32/verify_digest_soft.c` that took the slot for erased from one byte, refusing about one key in 256, fails the boot host suite.
