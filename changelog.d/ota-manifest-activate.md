### Added

- **`OTA_ACTIVATE` checks the SUIT-subset manifest on the ATmega2560's element
  tier** (row `ota`, owner ruling 121, the split gate). The application reads
  the envelope the stream carries after the image and holds it to the staged
  trailer through `tfw_suit_gate`, the reader the ESP32's layer 0 calls, before
  it writes the authorization or the `MARK`; a refused manifest leaves the store
  as `OTA_END` left it. Existing status codes only, no new command id.
  `tfw-boot`'s P-256 verify is unchanged on every row. The ATmega328P, the
  ATmega32U4 and the soft tier do not check the manifest (`docs/ota/engineering.md`).
- **`make image-seal` puts the envelope in the stream** on a board that checks
  it: after the image, in the padding before the trailer, a little-endian u16
  length then `make suit-envelope`'s envelope (ruling 109, 2026-10-22; OTA ADR
  decision 3, open question 5 settled). It refuses to run there without one;
  the region `tfw-boot` reads is unchanged, and `tfwctl ota-stage` sends the
  sealed stream as it is.
