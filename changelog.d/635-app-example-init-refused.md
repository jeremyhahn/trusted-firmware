### Added

- **A test of the example application on a HAL that refuses its pin** (#635).
  `test/unit/test_app_example.c` makes `tfw_hal_gpio_mode` refuse once: the
  example's init returns that refusal, the runner leaves the heartbeat FAILED,
  its pin not an output and never toggled, and answers TFW_ERR_UNSUPPORTED for
  evidence without writing the buffer; the next start, with the HAL accepting,
  runs it.
