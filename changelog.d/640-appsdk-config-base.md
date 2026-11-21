### Fixed

- **The application SDK guide gives the configuration region's real base** (#640). `docs/application-sdk/user.md` now puts `TFW_APP_CONFIG_BASE` at `TFW_BOOT_EE_LEN`, 0x140, not byte 192, has a host write it with `Device.EEPROMWrite` at `0x140 + offset`, and names `tfw_app_config_write`; no code changed. It also says that on an Ethernet image the region's first 32 bytes are the network block (`TFW_NETCFG_ADDR`), so such an image declares `config_len` 0, and that a host write overlapping the block needs the auth session (`TFW_ERR_AUTH_REQUIRED`).
