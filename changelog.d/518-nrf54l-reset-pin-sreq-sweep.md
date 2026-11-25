### Added

- **The nRF54L reset-cause test sweeps every bit beside `RESETPIN` and
  `SREQ`** (#518). `test_nrf54l_reset` already checked a watchdog bit beside
  each of the 32 bits; it now checks the reset pin and the reset request the
  same way, with and without the firmware's marker, so a decode that masks a
  debugger reset or a System OFF wake before its compare fails the test
  instead of reporting a pin press or the firmware's own restart. Test only:
  the decode is unchanged.
