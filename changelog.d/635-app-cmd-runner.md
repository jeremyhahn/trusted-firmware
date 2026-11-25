### Added

- **`APP_CMD` `0x07` reaches the application** (#635): a running application's `tfw_app_command` answers over serial, timed against the run budget; an application sets `APP_COMMANDS := yes` in its `app.mk` to define it, and otherwise links a stand-in that answers `TFW_ERR_UNSUPPORTED`.
