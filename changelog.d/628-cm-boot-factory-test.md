### Added

- **A host test pins the two-slot factory copy** (#628).
  `test_cm_boot_factory` runs `tfw_cm_boot_factory` on host memory mapped at
  the mps2-an505 BOOT_AB=1 addresses: a blank slot and platform block take the
  factory images QEMU loaded, byte for byte and nothing past their lengths; a
  programmed slot or platform block is never touched; a slot is blank when its
  stack-top word is zero, whatever follows; one barrier per call.
