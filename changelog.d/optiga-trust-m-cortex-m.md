### Added

- **`CRYPTO=optiga` builds on mps2-an505** (row `optiga-trust-m`), over the
  MPS2 I2C master on the shield 0 SBCon. The board's `BOARD_ROWS` gain
  `optiga:I2C=1`, a measurement row like the ATmega2560's two, so
  `make firmware-matrix` size-checks the tier on a Cortex-M. `BOARD_TIERS` does
  not name it and no default image changes. The transport needs no new HAL
  call. `test/unit/test_mps2_optiga.c` runs it on the MPS2 master over a
  simulated SBCon: the wire transactions, the late acknowledge, refused and
  stuck buses, and frames at both register limits. The onboarding ADR's sixth
  part gives the fit table and proposes an answer to question 5. No wire
  vector, command id or status code changes.

### Changed

- `CRYPTO=optiga` on a build with the I2C bus off stops in `mk/firmware.mk`
  with a message that names the bus, not at the link.
