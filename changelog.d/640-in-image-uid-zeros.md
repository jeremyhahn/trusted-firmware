### Fixed

- **`scripts/in-toolchain-image.sh` refuses a zero-padded `IN_IMAGE_UID` or
  `IN_IMAGE_CPUS`** (#640). Only the literal 0 was refused, so
  `IN_IMAGE_UID=00` ran the toolchain stage as root and `IN_IMAGE_CPUS=00`
  passed the positive-integer guard; any leading zero now exits 2 before
  docker is called.
