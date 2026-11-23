### Fixed

- **The STM32H5 docs name TF-M with our DICE partition as the secure world**
  (#586). The STM32H573I-DK board page, ADR #15's open-decision text
  (`docs/cortex-m/adr-trust-model-vs-tf-m.md`) and the onboarding ADR's slice
  table and follow-ups still named ruling 935's own TrustZone-M firmware. They
  now say what ruling 1024 recorded: upstream TF-M Profile Medium with our DICE
  layer as an Application RoT partition (ADR #15 (b1)); ruling 935's closure
  of the vendor secure worlds (no ST Secure Manager, no PSA token) stands.
  Documentation only.
