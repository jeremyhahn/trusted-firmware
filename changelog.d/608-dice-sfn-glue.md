### Added

- **The DICE partition's TF-M glue** (#608 slice 3, ruling 1024 (a)).
  `src/tz/tfm/tfw_dice_partition.c` is the partition's entry_init and secure
  function. At init it derives the UDS by HKDF-SHA256 from TF-M's hardware
  unique key (info "tfw-dice-uds-v1") and reads the non-secure image's
  measurement from Measured Boot slot 3. It starts the DICE service only if
  that slot is locked and holds a 32-byte SHA-256. Otherwise the service
  answers "not provisioned" and TF-M still boots. The UDS is zeroed on every
  path. `src/tz/tfm/CMakeLists.txt` builds it as the Application RoT library
  `tfm_app_rot_partition_tfw_dice`. `test_tfw_dice_glue` runs the glue on the
  host over stand-ins for the TF-M and Measured Boot calls.
