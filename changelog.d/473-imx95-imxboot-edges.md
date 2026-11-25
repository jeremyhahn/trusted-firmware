### Added

- **Host tests pin the end of the i.MX 95 M7's IMX_BOOT=1 measured region**
  (#473). test_provenance checks that a record at 0x0002FFFF, the last byte
  of the 192 KiB measured from ITCM 0, gives image_len 196608, that one at
  0x00030000 writes no manifest (the same byte is inside the slot's region
  in the default build), and that BUILD_DIR set to the repository's absolute
  build/ is refused like build/.
