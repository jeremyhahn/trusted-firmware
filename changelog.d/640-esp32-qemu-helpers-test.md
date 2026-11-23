### Added

- **`scripts/test_esp32_qemu_helpers.py`, a test of the ESP32 QEMU harness's helpers** (#640): the DeviceID point equals the C test's SEED_PUB, and the partition table, eFuse key words and boot log lines decode as ESP-IDF lays them out, with no ESP-IDF, esptool or QEMU. `make scripts-test` runs it.
