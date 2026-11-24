### Changed

- **The provisioning API and error table describe the TrustFLEX slot lock and
  public-key read-back** (#53). `PROV_SLOT_LOCK` on the UDS slot (`6`) takes
  `crc` `0` and uses the firmware's CRC-16 of the bytes `PROV_SEED_UDS` wrote
  this boot (`TFW_ERR_BAD_STATE` with no seed this boot, `TFW_ERR_BAD_ARGC` for
  a non-zero `crc`); the image slot (`15`) needs the host's non-zero CRC; a
  slot already slot-locked is `TFW_ERR_PROTECTED_REGION`, not
  `TFW_ERR_BAD_STATE`. `PROV_PUBKEY` reads slots `2` and `15` back from
  storage as well as recomputing slot `0`.
