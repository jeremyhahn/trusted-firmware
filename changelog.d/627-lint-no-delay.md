### Added

- **`make lint-no-delay`** (row `device-capabilities`, #627). The SPEC row's
  "There is no `delay()`" is now enforced: a `delay(`, `_delay_ms(`,
  `delayMicroseconds(` or `vTaskDelay(` call in the firmware C under `src/` or
  `boot/` fails `make lint` with `path:line: delay-call`. Comments and string
  contents are blanked before matching, with line numbers kept, so prose
  naming `delay()` passes and `/* wait */ _delay_ms(10);` or
  `delay /* ms */ (10);` still fails; `_delay_us`, the bus-timing primitive behind `tfw_hal_delay_us`,
  stays allowed. `lint-grep-selftest` plants a call and asserts the gate fails.
