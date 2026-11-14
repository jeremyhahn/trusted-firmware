### Fixed

- **i.MX 95 M7 tone retune** (#428). `hal_tone.c` sets the GPT's ENMOD
  before enabling it, so a retune restarts the counter from zero; before, a
  retune to a shorter half period than the count reached missed its compare
  until the 32-bit counter wrapped (about 179 s at 24 MHz).
- **i.MX 95 M7 PWM stays unsupported until a pad can be handed back** (#428).
  A stopped TPM channel kept its pad, so a later `DIGITAL_WRITE` did not
  reach the pin; the pad mux is the System Manager's. With
  `TFW_IMX95_PWM_PAD_HANDOFF` 0, no pin offers a PWM channel even with the
  TPM known, and `hal_pwm.c` refuses every call.
