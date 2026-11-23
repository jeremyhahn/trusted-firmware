### Added

- **The nRF54L tone test pins a retarget to another pin and a refused start**
  (#518). A tone moved from P1.10 to P1.14 must drive the old pin low and hand
  it back to the GPIO before the new pin is set up, and the stop after it must
  follow the new pin; a start refused for a bad pin while a tone plays must
  write nothing, leaving the playing tone untouched.
