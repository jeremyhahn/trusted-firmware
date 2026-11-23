### Added

- **A host test holds the STM32H5 PWM's refusal of a bad ref** (#628). A ref
  with channel 0 or 5, another unit, port 9 or pin 16 now gets
  `TFW_ERR_BAD_PIN` from both write and stop, and TIM5 and the GPIO clock stay
  untouched, as the MPS2 port's test already holds for its pins.
