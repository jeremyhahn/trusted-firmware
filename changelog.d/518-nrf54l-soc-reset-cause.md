### Added

- **Host tests for the nRF54L15's reset cause** (#518). Over the nRF54L
  register model, `make host-test` now checks that `tfw_cm_soc_reset_cause`
  reads RESET.RESETREAS, clears it by writing every bit, and reports SREQ as a
  software reset only under the `TFW_CM_MARK_RESETREQ` marker, unknown under
  the watchdog or panic marker or none.
