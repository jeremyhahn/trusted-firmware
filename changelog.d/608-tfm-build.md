### Added

- **TF-M v2.3.1 built for QEMU and the STM32H573I-DK** (#608 slice 2,
  ruling 1024 (a)). `make tfm-image TFM_BOARD=an521|stm32h573i-dk` runs
  `scripts/tfm-build.sh`, which fetches TF-M and tf-m-extras at their pins
  into a work directory outside the tree. It builds BL2, the secure image at
  Profile Medium with the Initial Attestation partition off (ruling 935), and
  tf-m-extras' example non-secure application, all signed. `make
  tfm-qemu-test` boots the an521 images on `qemu-system-arm -M mps2-an521`
  and waits for "Booting TF-M" and the non-secure application's first line.
  No TF-M source enters the repository.

### Changed

- **The TF-M pins are in one file**, `scripts/tfm-pins.sh`, which
  `scripts/tfm-measure.sh` and `scripts/tfm-build.sh` both source.
