### Added

- **`TestUploadProgressIsTheDevicesCount` in `sdk/go/upload_test.go`** (#629): after a failed OTA_DATA, `UploadProgress` reports the count OTA_STATUS gives, the chunk start when the frame was lost and the chunk end when only the reply was lost, never bytes the device does not hold.
