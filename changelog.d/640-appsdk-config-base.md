### Fixed

- **The application SDK guide gives the configuration region's real base** (#640). `docs/application-sdk/user.md` now puts `TFW_APP_CONFIG_BASE` at `TFW_NETCFG_ADDR + TFW_NETCFG_LEN`, 0x160, has a host write it with `Device.EEPROMWrite` at `tfw.AppConfigBase + offset` (0x160), and names `tfw_app_config_write`. It also says the network block `[0x140, 0x160)` lies below the region on every image, so an Ethernet image may own config.
