### Changed

- **docs/development/reproducible-builds.md re-verified** (#629): the check builds two `git archive HEAD` copies with `SOURCE_DATE_EPOCH` the commit time (`scripts/repro_build.py`), covers `APP_DIR` and `bootloader-reproducible-check`, and names only tiers a board accepts.
