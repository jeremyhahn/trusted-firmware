### Fixed

- **A timed pin whose revert the HAL refused is retried** (row
  `device-capabilities`, #627). `tfw_sched_poll()` used to free the timer
  whatever the GPIO write returned, so one refused revert at the deadline left
  the pin held with nothing left to drive it back. The timer now stays armed and
  the next sweep retries, as `TIMER_CANCEL` already does for its final level.
