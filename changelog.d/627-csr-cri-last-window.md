### Added

- **A host test asks `tfw_csr_cri` for every window at the tail of the
  CertificationRequestInfo and past it** (row `device-capabilities`). Offsets
  300 to 370 at caps 1, 59 and 60, then 0xFFFF: a window that starts inside the
  365 bytes yields what is left of them, the last byte included, and one that
  starts at or past the end is refused without writing either length.
