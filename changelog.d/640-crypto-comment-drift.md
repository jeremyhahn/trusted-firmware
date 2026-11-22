### Fixed

- **`src/crypto` and `src/store` comments match the code** (#640). They name
  `mk/firmware.mk` (not the absent `mk/avr.mk`) for `IDENTITY_SRCS` and
  `auth_session_none.c`, CRYPTO=auto's init-time choice, the 76-byte ATECC608A
  buffer, `tfw_atecc_verify_external`'s own reply path, ten rotates per SHA-256
  step, tfw-boot's five SPI NOR opcodes, and both places `verify_p256.c` is
  linked (HSM images and `ARCH_CRYPTO_WRAPS_SOFT`). Comments only: no code,
  wire format or EEPROM layout changed.
