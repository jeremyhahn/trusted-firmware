### Fixed

- **`include/tfw/app.h` and the application-sdk pages say a failed init ends
  an OTA trial at once** (row `application-sdk`). The `tfw_app_init` comment,
  `docs/application-sdk/api.md`, `docs/application-sdk/architecture.md` and
  `docs/application-sdk/user.md` said a failed init leaves the device
  firmware-only until the next reset, and the `tfw_app_after_update` comment
  and its api.md row did not say what a refused init or descriptor does on a
  trial boot. All now say what `src/core/ota_trial.c` does: on the trial boot
  of a newly installed image a refused init or descriptor resets into the
  bootloader at once, which rolls back (open question 1 of
  `docs/application-sdk/engineering.md`, decided A). Comments and docs only.
