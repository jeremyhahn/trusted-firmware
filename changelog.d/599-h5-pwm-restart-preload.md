### Fixed

- **A restarted STM32H5 PWM channel starts at its new duty** (#599). `tfw_hal_pwm_stop` now takes the stopped channel's CCR preload (OCxPE) off, so the write that restarts it loads its duty at once, as RM0481 states for a CCR with preload off, instead of into the preload register while the timer drives the pin at the duty from before the stop for up to one period.
