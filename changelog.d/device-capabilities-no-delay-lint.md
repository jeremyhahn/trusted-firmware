### Added

- **`make lint-no-delay`** (row `device-capabilities`). A grep gate in
  `make lint` refuses a call to `delay()`, `_delay_ms()`,
  `delayMicroseconds()` or `vTaskDelay()` in the firmware C under `src/`,
  `boot/`, `boards/` and `include/`, and prints `path:line: no-delay` only.
  The HAL's microsecond bus delays (`_delay_us`, `tfw_hal_delay_us`) and a
  comment naming the rule pass. `make lint-grep-selftest` plants each refused
  call. The row's "There is no `delay()`" requirement is now enforced.
