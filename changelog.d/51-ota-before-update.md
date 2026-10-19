### Fixed

- **`OTA_ACTIVATE` mode 1 tells the application of the update** (#51). Its
  reset installs the marked image, so the runner now calls
  `tfw_app_before_update` and then `tfw_app_shutdown`, as `RESET` mode 1 does,
  where it called `tfw_app_shutdown` alone. No update request is armed: tfw-boot
  finds the image by its `MARK`. No command id, status code or wire vector
  changes, and no RAM is added.
