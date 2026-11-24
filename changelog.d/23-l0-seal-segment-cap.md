### Fixed

- **The ESP32 layer 0 seal refuses an image with more than 16 segments** (#23): `boot/hal/esp32/l0_seal.py` walked every segment the header's count names, up to 255, where ESP-IDF's loader refuses an image with more than 16 and layer 0 measures only to the 16th, so such an image was sealed with a trailer image_len the board never computes; it is now refused.
