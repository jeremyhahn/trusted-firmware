### Fixed

- The ESP32 analog read takes its channel's pin off the tone and the PWM, as MODE and the SPI CS do, so a later tone on another pin no longer resets the ADC pad and turns its pull-up on (Refs #48).
