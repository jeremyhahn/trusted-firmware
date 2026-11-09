### Added

- **`tfw.ParseImage`, the OTA ADR's upload helper check** (#52). As section 13
  of `docs/ota/adr-ota-image-format-and-trust.md` spells it,
  `ParseImage(b []byte) (*Image, error)` checks an OTA stream's structure
  only: whole 256-byte pages, the trailer's magic and version 1, `image_len`
  non-zero and short of the trailer, `board_id` non-zero; and it splits off a
  manifest envelope appended the way `tools/image-seal` writes one. Each
  refusal is its own sentinel (`ErrImagePages`, `ErrImageMagic`,
  `ErrImageVersion`, `ErrImageLength`, `ErrImageBoard`, `ErrImageEnvelope`)
  wrapped with its numbers. Standard library only; no key; no signature
  check, which is the device's.
