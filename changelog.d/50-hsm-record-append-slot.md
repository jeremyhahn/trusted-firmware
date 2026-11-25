### Added

- **Tests for what an A6 record append hands the store, and a read past the
  count** (#50). `test_hsm_record_fail`'s fake store now records what it is
  given: an append puts the entry at the slot the head counts, with its fields
  and a zero spare, before the head of count plus one that keeps the dropped
  count, and a read at or past the count returns `TFW_ERR_RANGE` without a
  store get and leaves `out` untouched.
