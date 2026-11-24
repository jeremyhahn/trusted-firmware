### Added

- **The nRF54L15 SAADC host test covers an earlier owner's settings** (#518).
  The SAADC is shared with whatever ran before, so `make host-test` now leaves
  a differential PSELN, 256x oversampling and a nonzero SAMPLERATE on
  channel 0 and checks that a read puts PSELN back to NC, OVERSAMPLE to Bypass
  and SAMPLERATE to Task mode before it samples.
