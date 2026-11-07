### Added

- **`(*Device).Upload`, the OTA ADR's upload helper** (#52). As section 13
  of `docs/ota/adr-ota-image-format-and-trust.md` spells it,
  `Upload(ctx, *Image, ...UploadOption) error` stages `Image.Stream`:
  `OTA_BEGIN`, stop-and-wait `OTA_DATA` in `OTAChunk` pieces that fill
  `TFW_MAX_PAYLOAD` and never pass it, `OTA_END`. When an `OTA_DATA` fails it
  reads `OTA_STATUS` and, while the session is receiving, goes on from the
  device's count, so no accepted byte is sent twice. A count other than the
  start or end of the chunk in flight is `ErrUploadCount`; a failed session,
  a failed `OTA_STATUS` or a second failure of the same chunk returns the
  `OTA_DATA`'s error. `UploadProgress` reports each chunk. `StageImage` runs
  the same session loop without resuming; its behaviour is unchanged. The
  `Activate`/`Confirm` names are not implemented.
