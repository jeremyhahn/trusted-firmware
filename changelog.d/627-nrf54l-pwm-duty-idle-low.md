### Added

- **nRF54L PWM_WRITE tests: a duty after 255 idles the pin low, and the first
  write clears SHORTS, INTEN and SEQ0.ENDDELAY** (#627). test_nrf54l_pwm.c now
  fails if hal_pwm.c drops the duty 1..254 OUTCLR or any of the three
  configure() writes.
