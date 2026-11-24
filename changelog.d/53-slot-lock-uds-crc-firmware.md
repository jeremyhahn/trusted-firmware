### Changed

- **`PROV_SLOT_LOCK` takes the UDS slot's CRC from the firmware** (row 53,
  ruling 109). The host never holds the UDS, so for that slot the wire's `crc`
  is 0. The firmware locks against the CRC-16 of the 32 bytes this boot's
  `PROV_SEED_UDS` wrote. A nonzero `crc` for the UDS slot is
  `TFW_ERR_BAD_ARGC`. No seed this boot, a failed seed, or a second lock after
  a successful one is `TFW_ERR_BAD_STATE`. A zero `crc` for the image slot is
  `TFW_ERR_BAD_ARGC`. Nothing is sent to the element for any of these.
  Whether the real part's SlotLock CRC spans the whole 36-byte slot is still
  a bench question.
- **`PROV_PUBKEY` reads the stored-key slots** (the profile's image slot and
  slot 15), returning X||Y from their pad4|X|pad4|Y form, so
  `tfwprov --verify` can read slot 15. Slot 0 still uses GenKey public mode,
  and any other slot is still `TFW_ERR_RANGE`.
