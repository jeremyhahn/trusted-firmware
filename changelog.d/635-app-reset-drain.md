### Added

- **A host test pins the application's side of a RESET** (#635).
  `test/unit/test_app_reset_drain.c` checks that a RESET waits for its reply
  to drain, that a RESET(1) arms nothing until then, that a RESET(0)
  superseding a RESET(1) is a plain reset to the application, and that a
  RESET(1) superseding a RESET(0) is an update to it.
