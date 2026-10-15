### Added

- **`make hw-enroll DEVICE=<serial port> [DEVICEID=<hex>] [ACME_DIRECTORY=<url>]`**
  (#7) enrolls a provisioned element-tier board through device-attest-01 and
  writes a run record (board, transport, tier, ueid, verdict, UTC time, the
  issued chain in PEM) under `docs/development/enrollment-runs/`. The harness
  is `TestHardwareEnrollment` in `xkms/e2e`; `make xkms-test` runs it against
  `tfwsim`. It is outside `make ci`, as `bench-check` is. A board over TCP waits
  for an `sdk/go` tag carrying #59. Procedure:
  `docs/development/hardware-enrollment.md`.
