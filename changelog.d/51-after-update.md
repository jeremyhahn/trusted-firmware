### Added

- **`after_update` on the trial boot of a new image** (#51). After `init`
  returns `TFW_OK` on a boot that `tfw-boot` left in `TRIAL`, the firmware
  calls `tfw_app_after_update` once, before anything is served. A refusal ends
  the trial at once, as failed: the device resets and `tfw-boot` rolls back.
  `TFW_OK` confirms nothing; the trial runs on, and the control plane's
  `OTA_CONFIRM` confirms the image (OTA ADR decision 9). It is not called on
  any other boot, when `init` refused, or on a board without the trial (today
  every board but the ATmega2560). It uses the trial's own flag, with no new
  RAM, command id, status code or wire vector. The `application-sdk` row's
  last hook is shipped.
