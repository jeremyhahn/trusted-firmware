### Added

- **The one trial's two layers** (#77). tfw-boot arms the watchdog at the
  part's longest period, 8 s on AVR, after `TRIAL` is written and before the
  jump, on the trial path only (`boot_main.c` and the recovery session). The
  application's `.init3` still disarms it; before the application's `init`,
  `src/core/ota_trial.c` reads the platform block's `boot_state` and, in
  `TRIAL`, re-arms it. The superloop then pets it and counts
  `TFW_OTA_TRIAL_TIMEOUT_S`, 60 s on each AVR board with a boot section
  (PROPOSED, ADR open question 6), and resets into tfw-boot at expiry.
  `tfw_ota_trial_stop` ends the count; its caller is the `OTA_CONFIRM`
  handler (decision 8, PROPOSED), so for now an unconfirmed trial runs out
  and rolls back. New HAL entries: `tfw_hal_watchdog_arm`,
  `tfw_hal_watchdog_pet`. Only the 2560 links `ota_trial.c`; the 328P,
  32U4 and every other board link `ota_trial_none.c` until their boot can
  write `TRIAL`.
