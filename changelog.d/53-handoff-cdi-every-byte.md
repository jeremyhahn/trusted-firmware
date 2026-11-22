### Added

- **A host test that the handed-off CDI is read in full** (#53). `test_attest`
  hands over a CDI that is zero but for one byte, at each of its 32 positions,
  and checks `tfw_boot_handoff_take_cdi` takes it whole; only the all-zero
  "unprovisioned" sentinel is refused.
