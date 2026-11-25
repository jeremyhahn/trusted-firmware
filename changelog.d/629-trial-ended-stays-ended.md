### Changed

- **An OTA trial whose record left TRIAL before the deadline stays ended**
  (#629). `test_a_trial_the_record_ended_stays_ended` in
  `test/unit/test_ota_trial_mutate.c` confirms the record mid-trial, polls at
  the deadline, then puts TRIAL back and polls again: one pet, no reset. It
  holds `armed = 0u;` in `tfw_ota_trial_poll` (`src/core/ota_trial.c`), which
  no test caught before: without it an ended trial kept petting the watchdog
  and could still reset the device.
