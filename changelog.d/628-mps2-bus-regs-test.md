### Added

- **Host tests run the MPS2 bus and ADC drivers the image links** (#628).
  `test_mps2_bus_regs` compiles `src/hal/cortex-m/mps2/sbcon.c` and `pl022.c`,
  unchanged, on the register file and checks each access lands at its offset
  from the mps2-an505 base: SB_CONTROLS at +0x0, SB_CONTROLC at +0x4,
  SB_CONTROL masked to SCL|SDA, the PL022 at its base plus the offset, and FPGA
  I/O MISC at +0x4C. `test_mps2_adc` covers `hal_adc.c`, as its i.MX 95, ESP32
  and AVR siblings are covered: no reference to select, every channel out of
  range, and the caller's word never written.
