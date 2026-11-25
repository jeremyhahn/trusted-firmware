### Changed

- **The eth_socket backpressure test forces its send stall** (#55). Its reader
  is gated until the device's send has stalled on a full buffer, and the test
  asserts the 1000 ms stall poll ran, so it no longer depends on scheduling.
