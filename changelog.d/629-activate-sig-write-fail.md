### Changed

- **An OTA_ACTIVATE whose signature write fails marks nothing** (#629).
  `test_a_failed_signature_write_marks_nothing` in
  `test/unit/test_ota_activate_mutate.c` fails the MARK sector erase while
  `tfw_stage_authorize` stores the signature and holds the `return st;` after
  it in `tfw_otactl_cmd_activate` (`src/core/ota_activate.c`): ACTIVATE answers
  the store's status, writes no MARK and asks for no reset.
