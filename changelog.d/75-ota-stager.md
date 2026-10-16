### Added

- **The application-side OTA stager** (#75). An image with an external SPI NOR
  store answers `OTA_BEGIN`, `OTA_DATA`, `OTA_END` and `OTA_STATUS` the way
  tfw-boot does. It streams the image into the store's `STAGE` area, checks it
  against its trailer at `OTA_END`, and leaves a `MARK` record for tfw-boot.
  `ETH=1` on the 328P and 2560 links it, and `OTA_STAGE=1` does on the 32U4.
  Other images build as before. The chip select, D8, is a proposal (ADR open
  question 2).
- **tfw-boot copies a staged image** (#75). Built with `BOOT_STORE=1`, it
  reads `MARK` and checks the staged image again: trailer, board id, floor and
  SHA-256. Then it copies `STAGE` into the application region page by page,
  writing the trailer page last, and hands the image to its one trial boot as
  `PENDING`. A copy cut by a reset is redone on the next boot. `BOOT_STORE` is
  a new `mk/boot.mk` setting and the sixth field of a `BOOT_SUPPORTED` row. It
  is off by default, and only the 2560 without OTA in boot has a row with it
  on. The 328P and 32U4 rows, and the other 2560 rows, are unchanged. See
  `docs/ota/architecture.md`.
- **`sdk/go`: `OTABegin`, `OTAData`, `OTAEnd`, `OTAStatus` and `StageImage`**
  (#75) drive either tfw-boot's session or the stager. `tfwsim` carries the
  stager with a simulated NOR part, and the cross-test stages over serial and
  TCP.
- **The 2560's A/B swap-move** (#75). On the 2560, an image staged from the
  store or sent over tfw-boot's serial session lands in the secondary slot,
  and the primary stays whole while it arrives. A reset during a session
  boots the primary. Before any swap, the full gate (trailer, board id,
  floor, SHA-256 and signature) checks the image in the secondary, and a
  refused image is never swapped in. A good image is swapped into the 64 KB
  primary for its one trial boot (#82), and swapped back if the trial fails.
  `boot/boot_swap.c` keeps its place in a two-byte EEPROM record, so a swap
  cut by a reset is finished at the next boot. The 2560 rows with OTA or the
  store in boot link it; every other row links a stub.
