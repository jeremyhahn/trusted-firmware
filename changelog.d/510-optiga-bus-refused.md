### Added

- **A test that CRYPTO=optiga with the I2C bus off is refused** (#510).
  `scripts/test_optiga_bus_refused.py`, run by `make scripts-test`: `make
  firmware-srcs BOARD=mps2-an505 CRYPTO=optiga` stops with mk/firmware.mk's
  error, and with `I2C=1` it links the OPTIGA transport.
