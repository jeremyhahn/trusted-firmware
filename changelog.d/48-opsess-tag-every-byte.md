### Added

- **The ESP32-P4 operator session test holds every byte of the operator_bind
  tag** (#48). `test_esp_opsess` flips one bit in each of the tag's 32 bytes in
  turn and asserts the bound operator's own signature is refused each time, so
  a compare cut down to a prefix of the tag fails the test.
