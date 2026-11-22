### Fixed

- **The P4 page's failing OTA write and OTA_INFO rows name their follow-up**
  (#640). `docs/boards/esp32p4-poe.md` now tracks rows
  `OTAAuthLetsTheCommandPastTheGate` (stages an unsealed image) and `OTAInfo`
  (wants an element-tier peer), and the missing soft-tier lifecycle row, in
  #640.
