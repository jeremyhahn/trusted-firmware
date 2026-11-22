### Fixed

- **A test now pins that the eFuse-UDS backend scrubs the UDS it drew** (#53).
  genkey's buffer for the drawn UDS reads all zero by the time the DeviceID
  is derived through the part's HMAC. Before, deleting the scrub after the
  burn in `src/caps/provision_uds.c` passed every test, because the part's
  mock forgot the caller's buffer once the burn took; the secret would have
  stayed in a stack buffer for the next command to reuse.
