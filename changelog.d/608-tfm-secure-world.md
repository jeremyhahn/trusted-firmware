### Changed

- **The STM32H5 and nRF54L15 secure world is TF-M, with our DICE layer on top**
  (owner ruling 1024 (a)). ADR #15 and the MCU DICE attestation ADR take (b1),
  TF-M Profile Medium with our DICE layer as an Application RoT partition, in
  place of ruling 935's own secure monitor. No vendor secure world and no PSA
  token: TF-M is built without its Initial Attestation partition, or with it
  not exposed. The row plan records which boards TF-M v2.3.1 has a port for.
