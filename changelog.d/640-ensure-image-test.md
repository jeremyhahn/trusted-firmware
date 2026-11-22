### Added

- **A test pins `scripts/ensure-image.sh`** (#640): the image on the daemon, then the trusted image, then with `IMAGE_PULL=1` the registry, `make` last, and a refused `IMAGE_PUSH=1` push only warns; `make scripts-test` runs `scripts/test_ensure_image.py` over a fake docker and make.
