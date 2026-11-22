### Fixed

- **The host SDK's `Listener` godoc names the whole Ethernet allow-list** (#629): OTA_BEGIN, OTA_DATA, OTA_END and OTA_STATUS are dispatched over Ethernet in an image that links the stager (`src/proto/link_eth.h`).
- **`DefaultBootWait`'s godoc cites the real source of the 36 ms SHA bound** (#629): `TFW_ATECC_T_SHA` in `src/crypto/atecc608a/atecc.h`, from cryptoauthlib's ATECC608 table, not a datasheet figure.
- **`WithTimeout`'s godoc cites the real source of the Sign bound** (#629): `TFW_ATECC_T_SIGN`, cryptoauthlib's table, not the ATECC608A datasheet.
- **`ResetBootloader`'s godoc names both UNSUPPORTED cases** (#629): a board with no boot section, and an image whose update path is the external store (BOOT_AB with OTA_STAGE).
- **`EEPROMRead`'s godoc lists the ESP32-P4 hmac-p256 build as gate-enforcing** (#629): its profile turns AUTH_SESSION on; only an ESP32 board that leaves it off (the QEMU C3) keeps the open gate.
- **`EEPROMWrite`'s godoc states the network-block gate** (#629): a write overlapping NetConfigAddr..NetConfigLen returns StatusErrAuthRequired without an authenticated session where the gate is enforced.
