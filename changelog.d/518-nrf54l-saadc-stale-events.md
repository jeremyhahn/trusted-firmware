### Added

- **The nRF54L15 SAADC host test covers a second read** (#518). The SAADC
  leaves its STARTED, END and STOPPED events latched after a read, so
  `make host-test` now makes a good read, stalls the converter at start, end
  or stop, and checks that the next read times out with `TFW_ERR_TIMEOUT` and
  keeps the caller's value instead of taking the first read's events and
  sample.
