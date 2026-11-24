### Fixed

- **The MPS2 1-Wire host test proves the per-slot masking the board page
  states** (#628). test_mps2_onewire now counts every restore that takes
  interrupts back, so a reset, each write slot and each read slot must unmask
  after itself; a caller's own mask stays held through a reset, a write and a
  read; and a write or read of no bytes touches neither the line nor the clock.
