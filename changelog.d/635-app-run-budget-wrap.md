### Added

- **A test that the application runner's overrun count holds across the
  millisecond-counter wrap** (row #635). `test_app_runner_mutate` now starts a
  run up to `2 * TFW_APP_RUN_BUDGET_MS + 2` ms before the 32-bit counter wraps,
  for every run length either side of the budget, so a rewrite that compares
  against `start + TFW_APP_RUN_BUDGET_MS` instead of the difference fails.
