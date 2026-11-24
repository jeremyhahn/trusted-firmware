### Changed

- **ADC_REF passes an in-range reference the HAL refuses back** (#627): the HAL's status (the ESP32 HAL refuses `EXTERNAL`) is the command's answer, with no reply and the held reference unchanged.
