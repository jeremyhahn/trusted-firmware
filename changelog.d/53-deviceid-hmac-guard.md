### Fixed

- **A test now pins that the eFuse-UDS backend derives no DeviceID after a
  refused HMAC** (#53). genkey and pubkey called with a timed-out, crypto,
  internal or bad-state HMAC return that status and leave the caller's point
  buffer as it was. Before, cutting the status check ahead of the derivation
  in `src/caps/provision_uds.c` passed every test, because the wire handlers
  drop the point on any failure; the backend would have written a "DeviceID"
  derived from a seed the part never wrote.
