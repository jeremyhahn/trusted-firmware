### Fixed

- **The `spi_nor` store reads WEL back after WREN** (#234). A part whose WREN
  did not latch ignores the program or erase after it, and WIP then reads clear,
  so the driver answered `TFW_OK` for bytes it never wrote. It now reads the
  status register after each WREN and answers `TFW_ERR_NO_STORE` (20) when WEL
  is clear, before it sends the program or erase. A bus that echoes, with no
  part on the select, now fails a write and an erase the same way instead of
  passing them.
- **The `spi_nor` store refuses an address past 2^24** (#234). The command
  carries a 24-bit address, so a read, write or erase at or past 2^24 bytes, or
  a read or write that runs past it, wrapped to the bottom of the part. It now
  answers `TFW_ERR_RANGE` (11) and sends nothing.
