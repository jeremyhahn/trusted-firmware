### Added

- **The DICE partition in the TF-M build** (#608 slice 3, ruling 1024 (a)).
  `scripts/tfm-build.sh` adds `src/tz/tfm` and tf-m-extras' Measured Boot
  partition through TF-M's extra partition path
  (`TFM_EXTRA_PARTITION_PATHS`, `TFM_EXTRA_MANIFEST_LIST_FILES`). It turns on
  `TFM_PARTITION_TFW_DICE`, `TFM_PARTITION_MEASURED_BOOT`,
  `CONFIG_TFM_BOOT_STORE_MEASUREMENTS` and `MCUBOOT_DATA_SHARING`. The
  example non-secure app asks the partition for the DeviceID public key
  (`src/tz/tfm/ns/tfw_dice_ns_check.c`) and prints "tfw-dice: deviceid ok".
  The check joins the app through a CMake hook written to the work
  directory, not through a change to tf-m-extras. `make tfm-qemu-test` now
  requires that line after the non-secure marker.
