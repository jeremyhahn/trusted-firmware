### Added

- **`make lock-check`: the lock bits over the soft root's UDS, read back**
  (row device-provisioning, R6). After the operator's `avrdude` lock write it
  reads the lock byte over ISP and refuses to call the part provisioned unless
  every bit the board's profile names in `BOOT_LOCK_PROGRAMMED` reads
  programmed, naming each that does not. It writes nothing. The ATmega2560,
  ATmega328P and ATmega32U4 each name `LB1 LB2 BLB11 BLB12` from their data
  sheets, listed in `docs/reference/README.md`; a board that names none (the
  ATtiny85) is refused. `scripts/test_lock_check.py` runs it against a
  stand-in `avrdude` with literal lock bytes, under `make scripts-test`. No
  wire vector changed.

### Fixed

- **`docs/security/fuses-and-lockbits.md` asked for `BLB0` mode 3.** `BLB0`
  guards the application section, not the boot section (data sheet, Boot Loader
  Lock Bits); mode 3 would stop `tfw-boot` reading the image it verifies and
  writing an update. It stays unprogrammed, as the operator's `0x0C` already
  had it. The page now gives each board's lock byte and data sheet section; the
  same mistake in a `boot/hal/avr/boot_hal_avr.c` comment and in
  `docs/secure-boot/engineering.md` is corrected.
