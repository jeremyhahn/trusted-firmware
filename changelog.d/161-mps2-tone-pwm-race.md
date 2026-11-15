### Fixed

- **MPS2 tone with PWM** (#161). `hal_tone.c` lowers its LED bit with
  PRIMASK set on tone start and stop, as it already masked TIMER0. With
  `TONE=1 PWM=1`, the PWM dual-timer interrupt could change another LED
  between the tone's read and store of the LED word, and the store undid it.
