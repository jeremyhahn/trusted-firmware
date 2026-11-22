### Fixed

- **The STM32H5 HASH host model faults an SR clear while a digest is still in
  flight** (#599), so a driver that clears DCIS before CR.INIT, which would let
  a late digest's DCIS pass the next poll, now fails
  `test_h5_hash_clears_dcis_only_once_init_has_dropped_the_digest_in_flight`.
