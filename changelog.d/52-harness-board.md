### Added

- **The simavr `prev` row runs for the board named** (#52, row `ota`).
  `make integration-test-boot-prev BOARD=<board>` runs the harness's `prev`
  mode for any board with `PREV` in a store, and refuses one with `BOOT_AB=1`
  or no `OTA_STORE` before building; it is not in `ci`. The harness takes the
  store's chip select (`TFW_OTA_STORE_CS_IO`) and the UART (`TFW_UART_N`,
  USART1 on the 32U4) from the board profile instead of the 2560's. Pinned by
  `scripts/test_boot_prev_target.py` and `test_boot_harness_io` under the
  2560, 328P and 32U4 profiles. The row still stops at the boot budget gate:
  neither part's `BOOT_STORE=1` size is measured.
