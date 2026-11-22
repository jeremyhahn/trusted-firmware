### Added

- STM32H5 PWM HAL host test: a write and a stop clock the pin's GPIO bank in `RCC_AHB2ENR`, and a write takes the pull off the pin and routes its alternate function through AFRL for pins 0-7 and AFRH for pins 8-15 (Refs #599).
