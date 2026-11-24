### Changed

- **`tbs_hash()` in `src/core/suit.c` drops its short-length branch** (#1).
  The payload it hashes is the digest wrapper `suit_digest()` already
  accepted, 36 to 40 bytes, so its bstr head is always `0x58` and one length
  byte; the branch for a payload under 24 bytes could never run.
