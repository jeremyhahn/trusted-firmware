### Added

- **Tests for the A6 record over a store whose put, head put and get fail**
  (#640). `test_hsm_record_fail` now holds what `hsm_record.h` promises: a
  failed entry put fails the append before the head is put, a failed head put
  reaches the append, a failed get reaches the read, and a full record whose
  drop-count head put fails still returns `TFW_ERR_NO_SLOT` and puts no entry.
