### Fixed

- **include/tfw/protocol.h documents APP_CMD's refusals as the firmware answers them** (#635): an application with no commands answers `TFW_ERR_UNSUPPORTED` from the stand-in, and only an image without an application answers `TFW_ERR_UNKNOWN_CMD`.
