### Added

- **A test pinning the integrated SUIT reader's URI bound** (#50).
  `test_suit_integrated_mutate` reads a URI and payload name of
  `TFW_SUIT_URI_MAX` (32) characters, refuses one of 33 as the URI or as the
  name with `TFW_ERR_RANGE`, and refuses a payload name one character shorter
  than its URI.
