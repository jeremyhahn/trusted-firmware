### Changed

- **The TrustFLEX slot-map ADR is accepted as (a1)** by owner ruling 1243
  (row 53), gated on the bench reads, and names what the row applied. The
  device-onboarding pages say T3 (the 328P's flash budget and `ATTEST_CSR`) and
  T4 (the slot map) are approved and applied, and keep their bench halves open:
  `ATTEST_CSR` on a Nano, the 328P element-tier `ETH=1` image's RAM, the ADR's
  bench questions and `make hw-enroll` on a MIKROE-6774 board. User step 3
  shows the TrustFLEX template with `--image-key`. The adapter's read of the
  UCCS claims through `sdk/go` is marked done.
