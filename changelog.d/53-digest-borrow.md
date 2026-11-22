### Fixed

- **A test now pins that the DeviceID signer's RFC 6979 bits2octets carries
  its borrow** (#53). A digest n + 0xFF, whose reduction borrows out of the
  low byte, signs as its reduction 0xFF does, and the signature verifies.
  Before, the only digest above n the suite signed was FF..FF, which less n
  borrows in no byte, so a reduction that dropped the borrow passed every
  test while a DeviceID signed such a digest with the wrong k.
