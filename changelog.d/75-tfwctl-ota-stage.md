### Added

- **`tfwctl ota-stage <image>`** (#75). It reads the image file, stages it
  with `StageImage` (`OTA_BEGIN`, `OTA_DATA` in `OTAChunk` pieces, `OTA_END`),
  and prints the state and received count from `OTA_STATUS`. tfwctl does not
  parse the image; the device gates it. A refusal at any step prints the
  device's status and exits non-zero. The cross-test stages an image on
  `tfwsim`, and `tfwsim` refuses a tampered copy at `OTA_END`.
- **`tfwctl ota-status`** (#75). It prints the state and received count from
  `OTA_STATUS`, in the same form as `ota-stage`, and takes no arguments.
