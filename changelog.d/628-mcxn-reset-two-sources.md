### Added

- **Host tests pin the MCXN94x SRS decode's whole-word rule for every source**
  (#628): POR, VD, PIN or SW beside any other source is `TFW_RESET_UNKNOWN`,
  the WARM and FATAL class flags ride VD and SW too, and any nonzero
  `requested` (not only 1) makes a reset request the firmware's own.
