### Fixed

- The ESP32 tfw_hal_spi_xfer forgets its chip-select as the tone's pin and as PWM-driven once it has made it an output, as MODE does, so a PULSE_CAPTURE on a CS pin a tone or PWM once drove no longer loops its output back against the device (Refs #48).
