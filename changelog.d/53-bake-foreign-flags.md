### Fixed

- tfwprov `--bake-eeprom` and `--bake-netblock` refuse `--config`, `--yes-i-mean-it`, `--baud` and `--timeout`, which they accepted and ignored, so a record started by a bake with `--config` no longer looks template-checked when no template was read (#53).
