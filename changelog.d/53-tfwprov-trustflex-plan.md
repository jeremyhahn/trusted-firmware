### Added

- **`tfwprov`'s TrustFLEX plan for the pre-locked ATECC608C** (#53). A
  template with `plan = trustflex` (`sdk/go/templates/atecc608a-trustflex.conf`)
  prints, commits and verifies the (a1) steps of
  adr-trustflex-608c-slot-map.md in place of refusing the locked part: it
  reads the config zone and stops before writing if slot 6 or 15 is
  permanent, slot 0 is latched or either slot is already slot-locked, records
  the config zone and the slot-0 point, seeds the UDS into slot 6, writes
  `--image-key` to slot 15 and slot-locks both. The SDK gains
  `Device.ProvSlotLock`, `Device.ProvConfigRead` and `SlotTrustFLEXUDS`. Until
  the wire carries slot 6's lock CRC, which the host cannot compute, a
  TrustFLEX `--commit` refuses before writing anything.
