### Added

- **`scripts/test_app_static_check.py`, a test of `TFW_APP_STATIC_CHECK`**
  (#635). The host cc compiles the macro against `include/` with the feature
  gate or slot count set by `-D`: a capability the image has and slots the
  board has compile; a capability gated off, a bit outside the feature bitmap
  and more update slots than `TFW_OTA_SLOTS` are compile errors, each with
  its own message. `make scripts-test` runs it.
