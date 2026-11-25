### Added

- **A test for `tfwctl hsm-records` over an empty A6 record** (#50): a fresh
  or wiped engine prints `count 0, dropped 0` and no entry lines, and the
  `--json` form carries `"entries": []`, never `null`.
