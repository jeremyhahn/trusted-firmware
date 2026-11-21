### Fixed

- **The NUCLEO-H563ZI board page states its TZ=1 TrustZone-M split** (#628):
  the five SAU regions of `tz_map_nucleo_h563zi.h`, `make tz-firmware`, the
  host test `test_tz_regions_nucleo_h563zi`, and that the images are built and
  size-checked only, needing option bytes the owner sets and this tree never
  writes.
