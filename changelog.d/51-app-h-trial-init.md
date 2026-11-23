### Fixed

- **`include/tfw/app.h` says a failed init ends an OTA trial at once** (row
  `application-sdk`). The `tfw_app_init` comment said a failed init leaves the
  device firmware-only until the next reset, and the `tfw_app_after_update`
  comment did not say what a refused init or descriptor does on a trial boot.
  Both now say what `src/core/ota_trial.c` does: on the trial boot of a newly
  installed image a refused init or descriptor resets into the bootloader at
  once, which rolls back (open question 1 of
  `docs/application-sdk/engineering.md`, decided A). Comments only.
