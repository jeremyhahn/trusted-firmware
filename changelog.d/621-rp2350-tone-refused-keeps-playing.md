### Added

- **A host test pins that a refused RP2350 tone start leaves the playing tone
  running** (#621). `test_rp2350_tone` starts a tone, then has a 30 kHz start,
  a start on GPIO30 and a start on a bank-1 pin refused: no register is
  written, the timer interrupt stays enabled and armed, the old pin keeps its
  level, the refused pin is never driven, and the next alarm toggles the old
  pin on its old step.
