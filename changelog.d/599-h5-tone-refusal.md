### Added

- **A host test proves a refused STM32H5 tone start leaves a sounding tone as it
  was** (#599). A wrong pin or an unreachable rate while TIM3_CH2 toggles writes
  no register, keeps the counter at its old divider, and a later stop still
  forces the pin low and halts the timer.
