### Added

- **The soft DICE root's evidence, end to end** (#130). `tfwsim-auto` takes
  `--boot-tier soft`, the handoff a soft tfw-boot leaves. With no element on the
  bus, `tfwctl` E2E now proves the Mega's default image attests on the soft
  root: the evidence names it by its algorithm, HMAC-SHA256 under the soft CDI,
  where the element's is ES256; ATTEST_CERT answers unsupported; an element
  boot's CDI is refused; and every capability the image reports still answers.

### Changed

- **The `dice-attestation` row and the Mega's board page match main** (#130).
  The UDS is in boot-section flash, the 2560 default is `CRYPTO=auto` with six
  build rows, the evidence names its root by its algorithm, and the alias chain
  carries no root-type field until go-xkms names its encoding.
