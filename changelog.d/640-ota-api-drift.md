### Fixed

- **docs/ota/api.md cites its defines at their lines** (#640). `TFW_OTA_BEGIN_BUDGET` is `include/tfw/config.h:367` and `TFW_BOOT_EE_FLAG_REFUSED` is `include/tfw/boot.h:244`; a same-mode `OTA_ACTIVATE` retry while an activation is pending re-verifies nothing and writes nothing (`src/core/ota_activate.c:76-79`), as the page's table already said.
