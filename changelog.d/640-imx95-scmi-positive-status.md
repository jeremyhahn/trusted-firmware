### Fixed

- **i.MX95 SCMI: a positive status word is unrecognised** (#640). The
  specification numbers no positive status, but `tfw_scmi_check()` returned
  the platform's word unchanged, so a status of 1 read as the client's own
  `TFW_SCMI_E_TIMEOUT` and became `TFW_ERR_TIMEOUT`. A positive word is now
  `TFW_SCMI_E_UNNUMBERED`, which maps to `TFW_ERR_INTERNAL`.
