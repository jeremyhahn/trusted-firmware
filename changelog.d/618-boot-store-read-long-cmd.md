### Added

- **Host test for the Cortex-M OTA store's long READ** (#618).
  `test_store_mem` now sends tfw-boot's Cortex-M OTA store a READ longer
  than opcode and address, refused with nothing copied
  (boot/hal/cortex-m/boot_ota_store_cortex-m.c line 84).
