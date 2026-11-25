### Added

- **`scripts/test_app_mk.py`, a test of `mk/app.mk`'s `APP_DIR` gate**
  (#635). `make app-image` on a scratch application directory pins each
  refusal an out-of-tree application meets before anything is compiled: no
  `app.mk`, an `APP_NAME` outside `[a-z0-9][a-z0-9-]{0,14}`, an empty
  `APP_SRCS`, an absolute or `../` source, a source that is not `.c`, and one
  that does not exist; a valid application and a 15-character name are
  accepted. It needs make only, no compiler. `make scripts-test` runs it.
