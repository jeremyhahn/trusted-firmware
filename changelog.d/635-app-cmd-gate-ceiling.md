### Changed

- APP_CMD's overrun count stops at 0xFFFF, and the application's session view (`tfw_app_session_ok()`) reads closed when the auth gate is closed; both are now pinned by host tests (#635).
