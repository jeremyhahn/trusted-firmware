### Fixed

- **docs/cortex-m/api.md lists the MPS2 PWM, tone, pulse and 1-Wire HAL**
  (#628). The page said the MPS2 boards define no PWM, tone or pulse timing
  and answer 1-Wire with `TFW_ERR_UNSUPPORTED`; it now gives `hal_pwm.c`,
  `hal_tone.c`, `hal_pulse.c` and the bit-banged 1-Wire in `hal_bus.c`, each
  linked only in its `soft:PWM=1`, `soft:TONE=1`, `soft:PULSE=1` or
  `soft:ONEWIRE=1` row.
