### Added

- **A unit test pins the i.MX 95 TPM PWM's reprogram path** (Refs #628): a
  counter left at another period (stale MOD) or in center-aligned mode (CPWMS
  set) is stopped and reprogrammed by PWM write, in both host builds.
