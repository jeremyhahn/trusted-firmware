### Fixed

- **`docs/device-provisioning/engineering.md` names the platform block's length as `TFW_BOOT_EE_BLOCK_LEN`** (192), not `TFW_BOOT_EE_LEN` (0x140, 320, the end of the bootloader's EEPROM including the swap journal and the confirm request), in both places it gives the length.
