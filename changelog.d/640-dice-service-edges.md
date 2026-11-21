### Added

- **Host tests for the DICE service's refusal edges** (#640). `test_dice_service`
  now drives an init without a UDS, a UEID or a firmware hash (each refused, and
  a refused init forgets the last good one), an alias signature into a
  zero-length buffer, and a non-secure buffer whose range wraps the address
  space, at every entry point that checks one. Each is refused with nothing
  written.
