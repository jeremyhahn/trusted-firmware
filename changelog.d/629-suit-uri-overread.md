### Fixed

- **`tfw_suit_emit_manifest_integrated` refuses an install URI of 55 characters or more before it copies the sequence** (#629). The 64-byte install-sequence buffer in `tools/suit-emit/emit.c` was checked only after it had been copied into the manifest, so an over-long URI read past it on the stack; the call still returned 0 and wrote nothing. `test/unit/test_suit_emit_mutate.c` pins 54 characters (taken) and 55 (refused, nothing written).
