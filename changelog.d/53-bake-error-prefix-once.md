### Fixed

- tfwprov `--bake-eeprom` and `--bake-netblock` refusals print the `tfwprov:` prefix once, where they printed `tfwprov: tfwprov: ...` because the errors carried the prefix and the printer added it again (#53).
