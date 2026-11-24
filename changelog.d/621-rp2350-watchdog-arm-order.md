### Added

- **The RP2350 watchdog arm's test pins its order** (#621).
  `test_watchdog_arm_sets_up_before_it_enables` checks that the reset mark is
  set and PSM WDSEL and LOAD are written before the one write to CTRL enables
  the watchdog, and that CTRL is written once and not read back, so a reset in
  that window is neither misreported nor taken with the wrong block set.
