### Changed

- **The OTA ADR states who calls `tfw_app_after_update`** (#51). Its context
  and follow-up list said nothing called the hook; both now name
  `tfw_ota_trial_after_init()`, which asks it once on the trial boot, and the
  follow-up is marked done. Whether a failed `init` in `TRIAL` ends the trial at
  once is not settled by decision 9: it is open question 1 of
  `docs/application-sdk/engineering.md`, with three options and a
  recommendation, for the owner. No code changed.
- **A failed `init` ends an OTA trial at once** (#51). Open question 1 of
  `docs/application-sdk/engineering.md` is decided A: in `TRIAL`,
  `tfw_ota_trial_after_init()` resets into `tfw-boot`, which rolls back, when
  `init` refused or the descriptor failed its checks, by the path a refused
  `tfw_app_after_update` takes, so an image whose application cannot start can
  never be confirmed. Outside a trial a failed `init` still leaves the firmware
  serving. The OTA ADR's follow-up and the trial state machine in
  `docs/ota/architecture.md` name the new arrow. No command id, status code,
  wire vector or RAM byte changed.
