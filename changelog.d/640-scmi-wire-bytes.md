### Added

- **Tests for the i.MX 95 SCMI wire bytes and bounds** (#640): a clock_id
  byte with bit 7 set, an enable other than 0 or 1, a word index past the
  payload whatever the length says, and bit 7 of the Base protocol and agent
  counts are now checked.
