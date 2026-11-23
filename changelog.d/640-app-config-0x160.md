### Changed

- **Application config moved from 0x140 to 0x160** (#640, owner ruling 1234): config stored at 0x140 by an out-of-tree application must be re-provisioned at `0x160 + offset`, and a config write no longer reaches the Ethernet network block `[0x140, 0x160)` on any image; the host SDK gains `tfw.AppConfigBase`.
