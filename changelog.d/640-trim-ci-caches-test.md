### Added

- **`scripts/trim-ci-caches.sh` has a test** (#640): `make scripts-test` pins its argument checks and that the largest idle cache volumes go first, only until the total fits, with volumes in use kept.
