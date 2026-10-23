### Added

- **`make bootloader-candidate` measures a tfw-boot row before it is one**
  (#52, #75, row `ota`). `BOOT_CANDIDATE=<board>:<tier>:<measure>:<sig>:<ota>:<store>`
  builds tfw-boot for a row not in `BOOT_SUPPORTED` and prints its size against
  the boot section, or ld's overflow and `boot-core-size`; it exits 0 only when
  the row fits, and adds no row. It refuses, before building, a candidate that
  is not six fields, names no board, or is already a row. The 328P's and the
  32U4's restore rows, `atmega328p:atecc608a:atecc608a:atecc608a:0:1` and
  `atmega32u4:atecc608a:atecc608a:atecc608a:0:1`, are candidates: measured on the CI image, 1 718 and 1 708 bytes over the 4 096-byte section.
  Pinned by `scripts/test_boot_candidate_target.py`.
- **The split gate (ruling 121) is measured per tfw-boot row** (#78, row
  `ota`). `docs/ota/engineering.md` gives each row's boot bytes with and
  without the manifest field checks, its headroom, and the command that
  measures each number. No row needs the split. Every supported row fits with
  the full gate. The 328P and 32U4 restore rows are 1 718 and 1 708 bytes over,
  and moving the field checks out does not bring them within the boot section.
