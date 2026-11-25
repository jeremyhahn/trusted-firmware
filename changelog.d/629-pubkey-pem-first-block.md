### Fixed

- **`tfwprov --write-pubkey` refuses a key file whose first PEM block does not
  decode** (row `host-sdk`), even when a valid `PUBLIC KEY` follows it.
