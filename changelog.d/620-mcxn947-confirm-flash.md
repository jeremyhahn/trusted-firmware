### Fixed

- **OTA_CONFIRM's request reaches flash on the FRDM-MCXN947** (row #620).
  The EEPROM HAL stored the confirm request with a plain store into the
  platform window, which on the board is flash and was not programmed, so
  tfw-boot never saw the confirm. The request now goes through tfw-boot's ROM
  flash helper into the platform sector, a read-modify-write that keeps the
  platform block's other bytes, as rp2350 does since #500. User-sector writes
  still cannot reach the platform sector. Host-tested against the ROM flash
  model; not yet run on the board.
