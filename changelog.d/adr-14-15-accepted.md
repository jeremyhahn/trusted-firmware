### Changed

- **Cortex-M ADRs #14 and #15 accepted** (owner ruling 632 item 208 a,
  2026-11-14). The reference part is the i.MX 95 Cortex-M7 and the CI
  emulation is QEMU `mps2-an505` (#14 decisions 1 and 3, question 1 closed).
  The trust model is TF-M Profile Medium with our DICE attestation partition
  on the M33 secure world, and MPU-only isolation on the M7 (#15, question 1
  closed). The open questions the ruling does not answer stay open. The pages
  that called either ADR proposed now call it accepted. Documentation only.
