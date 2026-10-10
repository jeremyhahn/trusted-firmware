### Changed

- **`xkms/go.mod` requires `sdk/go v0.3.0-alpha`** (#28) and no longer
  `replace`s it with `../sdk/go`. A tagged module with a local replace is not
  consumable, so this unblocks tagging `xkms/`. Release order is now: tag
  `sdk/go`, bump `xkms/go.mod`, tag `xkms/`. The `xkms` tests find
  `tfwsim-atecc608a` through `TFWSIM_DIR` (the directory holding it), since
  the SDK now sits in the module cache rather than beside the adapter.
