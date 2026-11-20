### Added

- **TF-M on the M33, measured** (#483, ADR #15 follow-up F1).
  `scripts/tfm-measure.sh` fetches TF-M v2.3.1 and tf-m-extras at their
  pinned commits into a work directory outside the tree. It builds Profile
  Medium with BL2 for the STM32H573I-DK twice, as shipped and with
  tf-m-extras' example partition as a stub Application RoT partition, and
  then the example non-secure application. It prints the flash and RAM of
  each image from `arm-none-eabi-size`. No TF-M source enters the repository.

### Changed

- **ADR #15 carries measured figures for (b1)**
  (`docs/cortex-m/adr-trust-model-vs-tf-m.md`): the "on the order of 100 KB"
  estimate for TF-M with BL2 is replaced by the script's output, with the TF-M
  commit, the toolchain and the command. The decisions and open questions are
  unchanged. The (a) and (c) sizes have no build to measure yet and stay
  estimates.
