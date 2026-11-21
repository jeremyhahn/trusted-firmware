### Added

- **`scripts/tfm-measure.sh` has a test** (#640): `scripts/test_tfm_measure.py` runs it with stub tools and checks that flash is text + data, RAM is data + bss, the difference and sum rows add up, and a wrong pinned commit, a failed configure or a stub image no larger than base each stop it.
