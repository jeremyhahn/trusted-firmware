### Changed

- **The provisioning docs state the bake modes' wire-flag refusal, the positive `--baud`/`--timeout` rule, and the required template name** (#53): `docs/device-provisioning/cli.md` now says `--config`, `--yes-i-mean-it`, `--baud` and `--timeout` are refused beside `--bake-eeprom`/`--bake-netblock`, and that `--baud` and `--timeout` must be positive; `docs/device-provisioning/api.md` marks the template's `name` as required and non-empty.
