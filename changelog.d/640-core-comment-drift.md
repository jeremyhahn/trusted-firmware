### Fixed

- **`src/core` comments match the code** (#640). They count six HSM channel
  commands (HSM_RECORD_READ reads the A6 record), name `mk/firmware.mk` where
  they named a `mk/avr.mk` that does not exist, list every tier that links
  `auth_session.c`, `suit.c` and the HSM engine, name the ESP32's second
  `tfw_auth_session_init` call, the trial's reset on a FAILED or UNSUPPORTED
  runner state, `tfw_suit_expect`'s TFW_ERR_RANGE and the four crypto providers
  that call `tfw_secure_zero`. Comments only: no object changes.
