### Added

- **A host test pins that an RP2350 PWM write after a stop sets the slice up
  again** (#621). `test_rp2350_pwm` now checks the two caches
  `src/hal/cortex-m/rp2350/hal_pwm.c` keeps: a stop clears them, so the next
  write puts back the slice's divider, TOP, enable and the pin's pad; and a
  stop on a slice never written leaves its CSR and the CSR's clear alias
  alone while still handing the pin to SIO, driven low.
