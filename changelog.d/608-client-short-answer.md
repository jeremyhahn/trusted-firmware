### Added

- **The non-secure DICE client's answer-length checks have a test** (#608).
  A short key or signature, an empty or oversize alias-certificate page, or a
  short total from the SPM each answer `TFW_ERR_INTERNAL`, and the page
  length is not handed back.
