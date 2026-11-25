### Fixed

- **A test now holds the PREV store to an image that has not been tried yet** (#640).
  Nothing checked that `tfw_boot_swap_at_reset()` in `boot/boot_prev.c`
  copies PREV back only after a trial that ended unconfirmed. Dropping that
  check would copy PREV over every freshly staged image at reset, so every
  OTA on the ATmega328P would fail. The new host test fails if the check is
  removed.
