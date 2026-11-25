### Fixed

- **The SDK's CBOR reader refuses an argument in a wider head than it needs**
  (#629). The claims set is core-deterministic CBOR (RFC 8949 §4.2.1), which
  puts every argument in its shortest head, but `sdk/go/cbor.go` read 1, 2, 4
  or 8 bytes with no width check, so `18 05` decoded to 5 and a byte string
  with a 4-byte length of 3 decoded. It now refuses such a head.
  `TestCBORArgumentWidths` and `TestCBORLimits` pin the smallest value of each
  width and the refusals. The device writes preferred heads only, so no wire
  format, vector or device behaviour changed.
