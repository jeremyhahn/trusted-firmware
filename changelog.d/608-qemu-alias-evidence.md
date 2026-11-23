### Added

- **The QEMU TF-M gate checks the DICE alias evidence with go-xkms** (#608).
  The non-secure check now fetches the alias public key, the alias
  certificate over a fixed nonce and an alias signature over a fixed digest,
  and `make tfm-qemu-test` passes only when `xkms/cmd/tfm-dice-check` accepts
  them: the certificate signed by the DeviceID key, certifying the alias key,
  the nonce in its UCCS evidence (read with go-xkms's dice package), and the
  signature made by the alias key.
