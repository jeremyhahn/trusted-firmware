### Added

- test(imx95): the SAR ADC's error paths are pinned by host tests: a
  calibration that hangs or fails leaves the block powered down, a read
  before any init is refused with no register touched, and a read over a
  block left in scan mode with interrupts unmasked still converts once,
  polled (`test/unit/test_imx95_adc.c`) (Refs #50).
