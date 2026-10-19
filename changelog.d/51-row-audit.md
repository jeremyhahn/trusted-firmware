### Changed

- **The `application-sdk` row states what has shipped** (#51). The SPEC row
  said the application's evidence claims had not shipped; they have, in claim
  273, pinned by `TFWV_APPEV_WITH_UCCS`. The row now names what is still open:
  `before_update` when `OTA_ACTIVATE` mode 0 leaves the install to a later
  `RESET` mode 0, and `after_update`, whose call is the `ota` row's confirm.
  `docs/application-sdk/engineering.md` gains a Row status table: each
  requirement of the row, its status, where it is implemented and the test
  that pins it.
