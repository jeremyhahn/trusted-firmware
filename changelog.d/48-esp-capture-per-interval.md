### Fixed

- **The ESP32's polled `PULSE_CAPTURE` spends its timeout per interval** (#48): on the QEMU board and the classic ESP32, a capture now ends when the line goes quiet for `timeout_us`, not `timeout_us` after the call, as every other port does.
