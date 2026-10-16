### Changed

- **tfw-boot is built with `-mcall-prologues`** (#75). The 2560's OTA-in-boot
  row (serial session, swap-move, trial) goes from 8 190 to 7 832 of 8 192
  bytes, and every other boot row shrinks: 328P 3 758, 32U4 3 828, 2560
  3 918, 2560 with the store 6 302. Code generation only; the image decides
  what it did. The application is built as before.
- **`make bootloader` refuses an unsupported configuration** before building
  anything, with the message `boot-flash` gives. `BOOT_TIER=soft` with the
  element's measure and signature used to fail at the link on an undefined
  `tfw_atecc_sleep`. That message now keeps its full list of supported rows,
  which the commas in it used to cut short.
