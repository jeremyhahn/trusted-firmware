### Added

- **A Cortex-M fault record test for a stack pointer one or three bytes off**
  (#628). `test_cm_fault_frame` now refuses `sp` at a 1-byte and a 3-byte
  offset as well as the 2-byte one, so the alignment mask's low bit in
  `tfw_cm_fault_keep()` is held by the suite.
