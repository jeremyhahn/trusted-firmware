### Fixed

- **AVR tone retune** (#627). An AVR `TONE_START` that raises the pitch while
  a tone sounds no longer holds the pin for up to 256 ticks before the new
  pitch starts: `hal_tone.c` stops Timer2 and clears `TCNT2` before the new
  CTC top lands, so a top below the running count is not missed.
- **AVR tone after PWM on Timer2** (#627). A `TONE_START` after `PWM_WRITE`
  on a Timer2 pin sounds the requested pitch: `hal_tone.c` selects CTC before
  it writes `OCR2A`, so the top is not caught in the PWM mode's double buffer
  and the tone does not run at the old duty's compare value.
