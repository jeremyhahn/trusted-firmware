### Changed

- **A build without the HSM channel is pinned to refuse it** (row #50). The
  host unit tests now check that, with `TFW_FEATURE_HSM` off (the default), all
  six `HSM_*` ids (`HSM_HEARTBEAT`, `HSM_REPORT`, `HSM_VERDICT`, `HSM_SELECT`,
  `HSM_LOAD`, `HSM_RECORD_READ`) are unknown to dispatch. Each one is answered
  `TFW_ERR_UNKNOWN_CMD` with no reply bytes and recorded as refused. This is
  the only way a host can tell the channel is absent. A table row that slips
  out of `#if TFW_FEATURE_HSM` now fails the test by name.
