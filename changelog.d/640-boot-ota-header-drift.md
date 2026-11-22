### Fixed

- OTA state machine comment drift (#640): `boot/boot_ota.h` now names the in-place write as the single-slot case (the ATmega2560 and mps2 A/B builds stage in their secondary), says a verified in-place image goes STAGED -> TRIAL rather than straight to GOOD, lists the TFW_BOOT_APP_SIZE refusal at OTA_BEGIN, and says OTA_END has no partial page to pad because tfw_ota_begin admits only whole pages; the `boot/boot_ota.c` pad comment says the same.
