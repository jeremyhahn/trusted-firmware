### Added

- **An RP2350 boot flash write made with interrupts already masked leaves them
  masked** (#621). `test_boot_rp2350_rom` now enters `tfw_rp_flash_sector_write`
  with PRIMASK set and checks it is still set after the write, so a restore
  that unmasks unconditionally fails `make boot-soc-host-test`.
