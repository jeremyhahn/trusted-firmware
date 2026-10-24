### Fixed

- **`make reproducible-check` and `make bootloader-reproducible` build from a clean copy of HEAD** (row `secure-boot`): `scripts/repro_build.py` builds from `git archive HEAD` with `SOURCE_DATE_EPOCH` the commit time, so a stale `build/`, an untracked file or an uncommitted edit never reaches it; `bootloader-reproducible-check` (2560 soft base row) runs in `make build`; `scripts/test_repro_build.py` proves it.
