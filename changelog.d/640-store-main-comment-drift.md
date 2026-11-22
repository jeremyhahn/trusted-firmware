### Fixed

- **`src/main.c`, its host test and `src/core/ota_store.h` comments match the
  code** (#640): no `mk/avr.mk` (FW_SRCS builds the ADC on every image), the
  ATECC608A, auto and OPTIGA providers all re-init I2C, ESP32's `hal_eth.c`
  has its own empty eth poll, the trial poll is not the last call of a pass,
  and the store's erase, read and write name their `TFW_ERR_RANGE` and
  `TFW_ERR_NO_STORE` returns.
