### Added

- **A refused MPS2 tone start leaves a playing tone untouched** (#628).
  `test/unit/test_mps2_tone.c` starts LED1 at 1 kHz, then asks for port 0 bit 2
  (the first bit past the LEDs), port 0 bit 255, port 1 and 20001 Hz: each is
  refused before the timer stops, and the timer, its NVIC enable, the tone pin
  and the LED phase stay as they were. Test only: no firmware, wire format or
  status code changed.
