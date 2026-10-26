### Added

- **An `I2C=1` row on mps2-an500, and a QEMU I2C wire row on both MPS2
  boards** (row `cortex-m`, #161). `I2C=1` defines `TFW_CM_I2C` on
  mps2-an500 as on mps2-an505, which turns `TFW_FEATURE_I2C` on and links
  `src/caps/bus.c`, `hal_bus.c` and `sbcon.c` over the shield 0 SBCon at
  `0x40029000`; both boards' `BOARD_ROWS` gain `soft:I2C=1`, so
  `firmware-matrix` size-checks the image the wire row runs. The default build is
  unchanged. `make integration-test-cortex-m-wire` boots each board's soft
  image with `I2C=1` in QEMU and runs `TestQEMUI2CXfer`: with nothing on
  QEMU's bus every transfer returns `ERR_NACK`, and `0x60` and `0x80` are
  refused.
