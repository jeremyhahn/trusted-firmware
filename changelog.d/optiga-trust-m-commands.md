### Added

- **The OPTIGA Trust M read-only command layer** (row `optiga-trust-m`).
  `src/crypto/optiga/optiga_cmd.c` speaks OpenApplication, CloseApplication,
  GetDataObject (data and metadata) and GetRandom over the IFX I2C transport,
  each built as Infineon's *OPTIGA Trust M Solution Reference Manual*
  Revision 3.60 gives it (§4.4.1, Tables 40-42 and 45). The manual is pinned
  by SHA-256 in `docs/reference/README.md`. An error response reads the
  element's last error code back and maps Table 39 onto existing status codes;
  no status code is added. No write command is reachable, and a host test
  proves none is ever sent. No firmware tier links the layer, so no image
  changes size. No wire change.
