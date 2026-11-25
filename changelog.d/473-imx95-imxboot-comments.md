### Fixed

- **The FRDM-i.MX95 M7 profile's comments give `IMX_BOOT=1`'s measured
  region** (#473): under `IMX_BOOT=1` the region tfw-boot's provenance measures
  is ITCM 0, with `APP_BASE`; the profile.mk comment said it was "Not ITCM 0".
  board_profile.h and the provenance test now say why `TFW_APP_REGION_BASE`
  stays at the 0x00008000 slot: only tfw-boot and OTA staging read it, and this
  board builds neither.
