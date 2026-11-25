### Added

- **`test_app_none` pins the no-application measurement** (#635). With
  `src/core/app_runner_none.c` linked, as in every build without APP_DIR,
  `tfw_attest_measurement()` returns the firmware hash, both while the runner
  is started and after it is stopped, never whatever the buffer held.
