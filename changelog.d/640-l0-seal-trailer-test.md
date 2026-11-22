### Added

- **A test for the bytes the ESP32 layer 0 seal writes** (#640): `make scripts-test` checks that `boot/hal/esp32/l0_seal.py` lays trailer v1 out at `include/tfw/boot.h`'s offsets with its ES256 over the first 48 bytes, writes the image key's X || Y into the key slot file and prints its SHA-256 pin, and hands the RSA signer the image, the trailer, the envelope's length and the envelope, with no ESP-IDF and no cryptography package.
