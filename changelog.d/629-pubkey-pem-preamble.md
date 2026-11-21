### Fixed

- **`tfwprov --write-pubkey` refuses bytes before the PEM block** (row
  `host-sdk`). A key file with text before its PEM `PUBLIC KEY` block, a
  private-key block that does not decode included, was taken as the key; it is
  now refused before the port opens, as bytes after the block already were.
