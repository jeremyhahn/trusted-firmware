### Added

- **RP2350 peripheral row** (#429). `soft:PERIPH=1` on the Pico 2 W builds
  the I2C, PWM, ADC, tone and flash EEPROM implementations; the default row
  stays GPIO-only.

### Fixed

- **RP2350 rows build and stay inert by default** (#429). Several row
  features join into a one-word image directory (`-analog-pwm-tone-eeprom`);
  the flash EEPROM is linked only with the EEPROM feature; ADC init touches
  no PLL, clock or converter register in a row without analog inputs; tone
  startup and the tone handler both read the timer again after arming
  ALARM0, so a first or later alarm the timer passed (an interrupt between
  the read and the write, or a slow write) no longer stalls output for
  2^32 us.
