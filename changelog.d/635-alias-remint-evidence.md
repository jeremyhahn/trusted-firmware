### Added

- **`test/unit/test_alias.c`, a test that every ATTEST_ALIAS mint reads the
  application's evidence afresh** (#635). A second challenge carries the
  application's second value, ATTEST_CSR's measurement follows the new nonce
  and refuses the old one, pages read nothing, and an application that stops
  supplying evidence falls back to the firmware measurement. `make host-test`
  runs it.
