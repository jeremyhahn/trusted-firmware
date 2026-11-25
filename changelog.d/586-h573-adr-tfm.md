### Fixed

- **The STM32H5 docs name TF-M with our DICE partition as the secure world**
  (#586). `docs/multi-arch/adr-stm32h573i-dk-onboarding.md` still named "our
  own TrustZone-M firmware, not TF-M" in its ruling 935 callout, slice 6, the
  DICE map, the ADR history and open question 1. It now says what ruling 1024
  recorded in `docs/multi-arch/adr-mcu-dice-attestation.md`: upstream TF-M
  Profile Medium with our DICE layer as an Application RoT partition (ADR #15
  (b1)), not ST's Secure Manager, no PSA token. The STM32H573I-DK board page
  says the same, and ADR #15 (`docs/cortex-m/adr-trust-model-vs-tf-m.md`)
  names upstream TF-M Profile Medium in its decision text and marks its Secure
  Manager, vendor TF-M and PSA token offers not taken per ruling 935.
  Documentation only.
