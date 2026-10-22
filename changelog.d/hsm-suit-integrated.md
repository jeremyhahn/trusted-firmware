### Added

- **The SUIT reader takes a key and an integrated payload** (row
  `hsm-self-defence`, slice 1). `tfw_suit_read_integrated` verifies an
  envelope that carries its payload under a key the caller names, and refuses
  with existing status codes only. It is a file of its own, so the image
  reader, the application's gate, the ESP32 layer 0 and `tfw-boot` do not
  grow. `tools/suit-emit` emits the envelope, and new literal vectors signed by
  a second key sit beside the unchanged image vectors. No wire change.
- **Policy and script class ids, and the kind** (row `hsm-self-defence`,
  slice 2). Each board profile that holds an image class id now holds a
  policy and a script class id (ADR D2); a board without them has all zero
  and reads no envelope. `tfw_suit_read_integrated` refuses another vendor, a
  class not this board's or not one the caller expects, and a class that
  claims one kind while its payload carries the other, all with
  `TFW_ERR_UNSUPPORTED`; it returns the kind it read. New literal vectors
  follow the existing ones, which are unchanged. No new status, no wire change.
