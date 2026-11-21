### Fixed

- **The FRDM-MCXN947 page and its EEPROM HAL say what a power loss during
  OTA_CONFIRM can cost** (#640). Since #626 the confirm request is a
  read-modify-write of the platform sector through `tfw_mcxn_flash_update`,
  so a power cut between its erase and the end of its program can lose
  tfw-boot's platform block as well as the request, and the next boot goes to
  recovery until a programmer provisions it again. The page no longer calls
  the confirm write a plain store, and the "user sector only" power-loss
  promise now applies to user writes alone.
