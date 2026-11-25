### Added

- test/unit/test_esp_tone_mode_pulse.c pins MODE's tone release on the ESP32 for a tone moved to another pin (released only on the pin it now owns) and over a refused stop (the pin is taken back and NOTONE retries the stop) (Refs #48).
