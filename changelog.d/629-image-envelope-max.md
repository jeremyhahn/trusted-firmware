### Fixed

- **`tfw.ParseImage` refuses a manifest envelope longer than 320 bytes**
  (#629). The new `tfw.ImageEnvelopeMax` is `TFW_SUIT_ENVELOPE_MAX`
  (`src/core/suit.h`), the bound past which `src/core/ota_manifest.c` and
  `boot/hal/esp32/l0_gate.c` already refuse, so a stream the host checks as
  good is no longer one `OTA_END` rejects; the refusal is `ErrImageEnvelope`.
