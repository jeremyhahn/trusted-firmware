### Fixed

- **An application's configuration no longer overwrites the Ethernet network block** (row #640, owner ruling 1234). Application config starts at `TFW_APP_CONFIG_BASE` = 0x160 on every image, above the network block `[0x140, 0x160)`, so an application that owns config runs on ETH=1 and ETH_MAC=1 images too; the runner still refuses (UNSUPPORTED) a `config_len` past the end of the board's EEPROM.
