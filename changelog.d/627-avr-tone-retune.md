### Fixed

- **AVR tone retune** (#627). An AVR `TONE_START` that raises the pitch while
  a tone sounds no longer holds the pin for up to 256 ticks before the new
  pitch starts: `hal_tone.c` stops Timer2 and clears `TCNT2` before the new
  CTC top lands, so a top below the running count is not missed.
