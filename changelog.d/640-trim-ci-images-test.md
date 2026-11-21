### Added

- **`scripts/trim-ci-images.sh` has a test** (#640): `scripts/test_trim_ci_images.py`, run by `make scripts-test`, drives it over a fake `docker` and pins the newest `CI_IMAGE_KEEP` hash tags kept, non-hash tags left, the named image and in-use images never removed, `REAP_DRY_RUN=1` removing nothing, and a refused removal never failing the job.
