### Changed

- **The STM32H573I-DK onboarding ADR follows ADRs #14 and #15 as accepted**
  (owner ruling 632 item 208 a). Its section 3 now records (b1), TF-M Profile
  Medium with our DICE attestation partition, as the M33 decision, so the
  tension with ADR #14's tfw-boot expectation is gone, and its open questions no
  longer offer our own secure world (ADR #15 (a)) for this board. What stays
  open is whether the partition sits on ST's Secure Manager or upstream TF-M.
