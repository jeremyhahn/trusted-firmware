### Changed

- **`make lint-no-delay`** (row `device-capabilities`) also sweeps the
  firmware C under `boards/` and `include/` when present, not only `src/` and
  `boot/`. `make lint-grep-selftest` plants `_delay_ms()` in `boot/`,
  `delayMicroseconds ()` in `boards/` and `vTaskDelay()` in `include/`, and a
  `_delay_us` call, a `tfw_hal_delay_us` call and comments naming the refused
  calls must pass.
