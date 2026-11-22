### Fixed

- **test/mock's AVR stand-ins and runner fake name every unit and suite that
  uses them** (#640). `util/atomic.h` names hal_gpio.c, hal_pwm.c, hal_tick.c
  and hal_tone.c beside hal_uart_soft.c; `util/delay.h` names hal_sys.c;
  `avr/eeprom.h`, `avr/pgmspace.h` and `avr/interrupt.h` name the bootloader's
  boot_hal_avr.c (and recovery_halt.c for `cli`); `avr/wdt.h` names
  hal_early.c's disarm; `runner_fake.h` names the test_ota_trial_leave link.
  Comments only.
