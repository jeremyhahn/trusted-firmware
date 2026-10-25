- esp32: `make lint-arch-leak` now also enforces that ESP-IDF stays behind the
  HAL: an ESP-IDF header include or ESP-IDF name outside `src/hal/esp32/`,
  `boot/hal/esp32/`, `src/third_party/` and the ESP32 boards fails the target,
  naming the file; `scripts/test_esp32_port.py` drives the target on planted
  leaks instead of scanning the tree itself. `src/hal/esp32/prov_uds.c` has a
  host suite (`test_esp_prov_uds_mutate`) covering every path, refusals included.
  The SPEC `esp32` row's text moved to `docs/esp32/engineering.md`.
