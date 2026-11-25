### Added

- **A host test that the eFuse-UDS backend refuses every slot but the
  DeviceID's** (#629). PROV_GENKEY and PROV_PUBKEY with slot 1 or 0xFF return
  TFW_ERR_RANGE and send no point, before the random source is drawn or the
  part burned, and before the not-provisioned or already-burned checks.
