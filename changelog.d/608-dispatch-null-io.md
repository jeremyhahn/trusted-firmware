### Added

- **The DICE dispatcher's missing-callback check has a test** (#608). A
  message whose glue left its read or its write callback unset answers
  `TFW_ERR_RANGE` before any operation runs, and nothing is written.
