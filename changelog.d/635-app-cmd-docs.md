### Changed

- **The application wire-commands ADR is accepted** (#635, owner ruling 1243): `APP_CMD` `0x07` with an `app_op` byte, serial only, is documented in the wire table, the application API, engineering and architecture; the device's answer to `0x7F` stays open.
