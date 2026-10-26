### Added

- **tfw-boot on the FRDM-MCXN947** (row `mcxn947`, slice 2). The boot row
  `frdm-mcxn947:soft:soft:p256-soft:0:0` joins `BOOT_SUPPORTED`, with P-256
  verify like every row. `boot/hal/cortex-m/mcxn94x/` adds a polled LPUART4
  on the application's pins and rate arithmetic (`lpuart_baud.c`, now in the
  boot's object list too), and a flash writer through the boot ROM's flash
  driver: the tree at `0x1303FC00`, `flash_init`, `flash_erase` with
  `kFLASH_ApiEraseKey`, `flash_program`, 8 KiB sectors written whole. Each
  value is cited to the MCUXpresso SDK file and line at its pinned commit
  (ruling 109). Two host tests, `test_mcxn_uart` against a register model
  and `test_mcxn_flash` against a ROM model, join `BOOT_TEST_BINS`. The
  image's fit in its 32 KB region is not yet measured, and no board has run
  it. `scripts/test_cortex_m_flash.py` now pins the board's `boot-flash`
  probe lines like the other three boards' (slice 3), where it had
  asserted the board had no boot row.
