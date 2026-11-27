### Added

- **The STM32H5 sign yield's rx drain is now tested** (`test_h5_sign_yield`,
  #599). Every `tfw_sign_yield()` during a P-256 sign must read received frames
  into lwIP before the lwIP timers and lease expiry run, so a TCP peer
  answering keepalive during a long ATTEST sign is not found dead. The new test
  counts one drain per deadline sweep and no sweep ahead of its drain, so
  dropping the drain or running it after the sweep now fails.
