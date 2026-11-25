### Added

- **The Cortex-M pulse timer's failed pin read is tested** (#640).
  test_cm_pulse's model pin can fail one read: `tfw_hal_pulse_in` returns the
  read's status with the width untouched, before and during the pulse, and
  `tfw_hal_pulse_capture` returns it with no edges reported, even after it
  has recorded some.
