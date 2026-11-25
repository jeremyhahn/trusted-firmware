### Fixed

- **`tfwctl --sig-file` reads a file of hex text as hex at any length** (#629).
  A file of 64 hex digits, half the authorization, is refused as 32 bytes
  before anything is sent, instead of going to the device as 64 raw bytes.
