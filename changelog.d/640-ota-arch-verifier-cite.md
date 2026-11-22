### Fixed

- **The OTA architecture page's option (i) refusal cites the soft provider's
  `tfw_crypto_verify_image` stub at its line on main** (#640): the cite pointed
  at `src/crypto/soft/provider.c:258`, past the end of the 246-line file; it
  now names line 242, where the stub returns `TFW_ERR_UNSUPPORTED`.
