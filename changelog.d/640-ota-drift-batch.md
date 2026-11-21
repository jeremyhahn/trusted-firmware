### Fixed

- **The OTA docs and comments match the code** (#640): the ADR's `TFW_BOOT_EE_FLAG_REFUSED` cites point at `include/tfw/boot.h:244` and `0x6F` is named `OTA_AUTH`; `docs/ota/README.md`'s board table has its ATmega2560 and ATmega32U4 rows again; `ota_ctl.h`, `ota_ctl.c`, `ota_stage.h` and `sdk/go/ota.go` place `OTA_ACTIVATE` in `ota_activate.c` and the MARK with `OTA_ACTIVATE`, not `OTA_END`.
