### Added

- **The FRDM-MCXN947 PWM_WRITE host test checks CTIMER0 itself** (#628). Duty
  0 must halt the timer (TCR = CRST, no PWM channel) and the next nonzero write
  must start it again (TCR = CEN, match 0 at the new duty). A pin with no
  compare unit (LED_GREEN) or past the table must be refused with its PCR left
  as it was.
