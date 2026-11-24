### Added

- **The FRDM-MCXN947 link UART test covers a full receive ring** (#628). With
  63 bytes unread the 64th is read from DATA and dropped, the 63 come out in
  order, and the ring takes bytes again once drained, across the wrap of both
  indices.
