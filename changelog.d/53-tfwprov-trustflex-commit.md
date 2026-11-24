### Changed

- **`tfwprov --commit` provisions a pre-locked ATECC608C-TFLXTLS** (#53). Step
  6 slot-locks slot 6 with the new `Device.ProvSlotLockUDS` (`[6,0,0]`: the
  firmware takes the CRC of the UDS this boot's `PROV_SEED_UDS` drew), then
  slot 15 over `--image-key`'s CRC, so the TrustFLEX template's `--commit` is
  no longer refused. `SlotUDS` is 6, the slot map every board builds;
  `SlotTrustFLEXUDS` is removed and `SlotImageSigning` (slot 2, which no
  shipped image accepts) is deprecated for `SlotSecureBootKey`. `tfwsim-prov`
  builds with the boards' slot map and gains `--trustflex-element`, a
  pre-locked part the e2e tests commit, read back and verify.
