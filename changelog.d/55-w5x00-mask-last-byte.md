### Fixed

- **The W5x00 suite pins every byte of the subnet mask** (#55).
  `test_start_programs_every_byte_of_a_mask_that_does_not_end_in_zero` in
  `test/unit/test_w5x00.c` starts the driver with a /29 mask
  (255.255.255.248) and checks that all four bytes reach the chip's SUBR
  register. Before, the suite's only mask was a /24 whose last byte matches
  SUBR after a reset, so a driver that wrote three bytes of SUBR
  (`src/net/w5x00.c:117`) left every Ethernet suite green.
