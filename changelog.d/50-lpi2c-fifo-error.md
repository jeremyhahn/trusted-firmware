### Added

- **Host tests of the i.MX 95 LPI2C master's FIFO error and init's clock
  refusal** (#50). `test_imx95_lpi2c` raises FEF on a data byte and shows the
  transfer is `TFW_ERR_INTERNAL` with both FIFOs flushed and a STOP sent, and
  shows that `tfw_imx95_lpi2c_init` refuses a rate it cannot make with
  `TFW_ERR_RANGE` before touching a register, leaving the running master as it
  was.
