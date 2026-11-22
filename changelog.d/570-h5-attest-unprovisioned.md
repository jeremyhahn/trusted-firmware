### Fixed

- **STM32H573I-DK attest refusal row** (#570, epic #543).
  `TestFirmwareAttestRefusedUnprovisioned` timed out on the board because the
  bench boot (`cm_image`'s `boot.bin`) carries a UDS, so the soft tier is
  provisioned and `attest` signs instead of refusing. Measured 2026-11-17:
  one `attest` on the provisioned image answers with evidence in 27.7 s
  against a 2.0 s CAPS round trip, about 26 s of software P-256 at 64 MHz,
  past the suite's 5 s timeout. On `boot-unprovisioned.bin` (the same boot,
  UDS slot erased) `attest` is refused at once and the row passes. The row
  stays held from the bench boot's list, run by name on the unprovisioned
  boot; `docs/boards/stm32h573i-dk.md` has the steps.
