### Changed

- **A TIMER_SET whose pin the HAL cannot make an output is held by a test**
  (#629). `test_a_timer_on_a_pin_the_hal_cannot_make_an_output_is_not_armed`
  in `test/unit/test_caps_faults.c` holds the `return st;` after
  `tfw_hal_gpio_mode` in `tfw_sched_cmd_set` (`src/caps/sched.c`): the host
  hears the HAL's status, no timer is armed, no slot index is written, and
  the pin is neither made an output nor driven.
