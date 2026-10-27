### Added

- **`PROV_WRITE_PUBKEY` (`0x7B`) and `tfwprov --write-pubkey <file> --slot <n>`**
  (#52). The command writes a P-256 public key into slot 2 (the boot
  image-signing key) or slot 15 (the TrustFLEX secure-boot key). It sends X,
  then Y, and answers with the slot read back. The element refuses it while the
  config zone is unlocked (`BAD_STATE`) and for a slot whose SlotLocked bit is
  clear (`PROTECTED_REGION`). The UDS backend answers `UNSUPPORTED`.
  `Device.ProvWritePubKey` fails when the read-back differs from the key.
  `tfwprov --write-pubkey` takes a raw 64-byte X‖Y or a PEM `PUBLIC KEY`. It
  refuses a private key, another curve and a point off P-256, naming why,
  before it opens the port. The frames are pinned in `testdata/vectors.h`.
  `e2e_writepubkey_test.go` runs the CLI's path against tfwsim-prov.
