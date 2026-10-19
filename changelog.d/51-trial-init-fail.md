### Changed

- **The OTA ADR states who calls `tfw_app_after_update`** (#51). Its context
  and follow-up list said nothing called the hook; both now name
  `tfw_ota_trial_after_init()`, which asks it once on the trial boot, and the
  follow-up is marked done. Whether a failed `init` in `TRIAL` ends the trial at
  once is not settled by decision 9: it is open question 1 of
  `docs/application-sdk/engineering.md`, with three options and a
  recommendation, for the owner. No code changed.
