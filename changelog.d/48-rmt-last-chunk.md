### Added

- **The RMT pulse capture's host test checks the driver's last chunk** (#48).
  `test/unit/test_esp_pulse_rmt.c` queues a last buffer with no end marker and
  fewer intervals than asked for, and asserts the capture returns on its
  delivery, not at its `timeout_us * (max_edges + 2)` deadline. Test only;
  `src/hal/esp32/hal_pulse_rmt.c` is unchanged.
