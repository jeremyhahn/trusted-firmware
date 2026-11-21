### Changed

- **`docs/security/fuses-and-lockbits.md` re-verified** (#640): it names
  `make boot-flash`'s EESAVE guard and `BOOT_FIRST_FLASH`, the factory record
  and `make board-restore`, and the ATtiny85 that `make lock-check` refuses;
  its order takes the factory record before any fuse write, since
  `BOOT_FIRST_FLASH=1` writes none once `EESAVE` reads programmed.
