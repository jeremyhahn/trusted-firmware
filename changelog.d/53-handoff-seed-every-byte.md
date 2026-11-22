### Added

- **Host test that the handoff's DeviceID seed is read in all 32 bytes** (#53).
  `test_appstate_mutate` now hands over a seed that is zero in every byte but
  one, at each of the 32 positions, and checks that each is taken whole; the
  all-zero seed is still refused with the caller's buffer untouched.
