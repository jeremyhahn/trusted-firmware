### Fixed

- The ESP32 1-Wire reset, write and read take their pin off the tone and the PWM, as MODE and the SPI CS do, so a later tone on another pin no longer resets the 1-Wire pad (Refs #48).
