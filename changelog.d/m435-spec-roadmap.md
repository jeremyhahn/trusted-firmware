### Changed

- **SPEC and ROADMAP follow the owner ruling of 2026-10-17** (m435). The
  ATECC608C is never required on an AVR board: when no element answers, the
  firmware defaults to a soft DICE root with the UDS under lock bits, and the
  evidence says so (#130); the element tier is an additional Mega row. The
  `ethernet-transport`, `ota`, `device-provisioning` and `application-sdk`
  rows carry the ruling's items (`ETH=1` default on the 2560, one update path
  per image, `OTA_INFO`/`OTA_CONFIRM`/`OTA_ACTIVATE` at 0x6C–0x6E, EESAVE at
  the first boot flash, claim 273 shaped by go-xkms), and the STM32H5,
  OPTIGA Trust M, RP2350 and nRF54L15 rows link their onboarding ADR issues
  (#126–#129). Docs only; no code, vector or wire change.
