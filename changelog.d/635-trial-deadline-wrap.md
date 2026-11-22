### Added

- **A host test pins the OTA trial deadline across the 32-bit millisecond
  wrap** (#635). A trial started 1 s before `tfw_hal_millis()` wraps resets
  neither before the wrap nor after it short of the deadline, and resets at it.
