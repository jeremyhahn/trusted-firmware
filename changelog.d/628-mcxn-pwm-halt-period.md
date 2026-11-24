### Added

- **Two MCXN94x CTIMER PWM halt tests in `test/unit/test_mcxn_ctimer_pwm.c`** (#628): halting match 3, the PWM period, leaves MR3 at 254, the running channel and the counter alone, and with no channel running it stops the timer and holds it in reset.
