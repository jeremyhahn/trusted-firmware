### Fixed

- **No test pinned that `HSMRecordReadAll` stops at the entry's Count**
  (#50, `sdk/go/hsm.go`). Deleting the Count stop sent one read past the
  record's end and every test still passed. The two-entry test now checks the
  two reads it sends, and a new test reads a full record (32 entries,
  `TFW_HSM_RECORD_MAX`) over a device that would answer a 33rd and checks 32
  reads.
