### Fixed

- OTA trial comment drift (#640): `boot/boot_trial.h` and the trial API listing in `docs/secure-boot/engineering.md` no longer say OTA_CONFIRM and OTA_ACTIVATE have no wire id; they name 0x6D and 0x6E, the two callers of `tfw_boot_trial_confirm` (the application's OTA_CONFIRM and tfw-boot's settle at reset), say `tfw_boot_trial_activate` is still reached only from tests because the wire's OTA_ACTIVATE writes the store's MARK, and the listing now includes `tfw_boot_trial_fall_back`.
