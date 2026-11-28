### Added

- **The STM32H5 HASH driver's BUSY wait is tested per block** (#599).
  `test/unit/test_h5_hash.c` holds BUSY set: a two-block message times out
  with exactly its first 16 words written, no DCAL and the output untouched;
  a 60-byte message still digests correctly without one BUSY read
  (`src/crypto/stm32h5/h5_hash.c`). Test only; the driver is unchanged.
