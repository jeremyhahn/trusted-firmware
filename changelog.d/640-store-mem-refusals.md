### Added

- **Host tests for the Cortex-M OTA store's refusals** (#640, row boards).
  `test_store_mem` now drives the six refusal branches of
  boot/hal/cortex-m/boot_ota_store_cortex-m.c no test took: a JEDEC, status
  or write-enable command of the wrong length (a refused write enable sets no
  latch), a program shorter than its address or with a read length, a
  program running past the span, and a read starting past the span. Each is
  refused with nothing read or written.
