### Added

- **`AlgID` 0x11: ES256 signed by the software DICE root** (#130). The Mega
  2560's soft root signs ES256 with its P-256 DeviceID, as the element does, so
  the algorithm alone no longer named the root. An `ATTEST` frame signed by the
  soft root now says `0x11`; the element's stays `0x01` and the soft HMAC
  root's `0x10`. The alias certificate and CSR still sign `ecdsa-with-SHA256`.

### Changed

- **The soft root's ES256 evidence reads `0x11`, not `0x01`** (#130). A
  verifier that does not know `0x11` reads it as unrecognised; go-xkms must
  accept it before it appraises a soft-rooted Mega. The `dice-attestation` row
  and its requirements table say the evidence names its root on every board.
