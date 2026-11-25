### Added

- **`scripts/test_app_runner_none.py`, a test of the empty runner's surface**
  (#635). Every `tfw_app_runner_*` that `src/core/app_runner.h` declares is
  defined in both `src/core/app_runner.c` and `src/core/app_runner_none.c`,
  the empty runner defines no runner call the header lacks, and it defines no
  `tfw_app_cmd_info` and no `tfw_app_config_*`, so an image without an
  application links no APP_INFO handler and no config span. `make
  scripts-test` runs it.
