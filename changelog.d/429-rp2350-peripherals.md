### Added

- **RP2350 peripheral rows** (#429). On the Pico 2 W, `I2C=1` builds the
  I2C master and `PERIPH=1` the PWM, ADC, tone and flash EEPROM
  implementations; the two are separate selections, so an image with all
  five takes `I2C=1 PERIPH=1`. The default row stays GPIO-only. Until the owner lifts the AVR gate for rp2350, the
  `I2C=1` and `PERIPH=1` rows are build and host-test only: `make flash`
  refuses them before any probe command.

### Fixed

- **RP2350 rows build and stay inert by default** (#429). Several row
  features join into a one-word image directory (`-analog-pwm-tone-eeprom`);
  the flash EEPROM is linked only with the EEPROM feature, and a write
  fails unless the whole rewritten sector reads back as expected, and a
  SysTick expiring as the write starts is counted once, not twice; ADC init touches
  no PLL, clock or converter register in a row without analog inputs; tone
  startup and the tone handler both read the timer again after arming
  ALARM0, so a first or later alarm the timer passed (an interrupt between
  the read and the write, or a slow write) no longer stalls output for
  2^32 us.
