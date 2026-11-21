### Fixed

- **An application's configuration no longer overwrites the Ethernet network block** (row #640). On an ETH=1 or ETH_MAC=1 image the runner refuses an application with a nonzero `config_len` (UNSUPPORTED), and `tfw_app_config_read`/`tfw_app_config_write` return `TFW_ERR_PROTECTED_REGION`; the EEPROM layout is unchanged.
