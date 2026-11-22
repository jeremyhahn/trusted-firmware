### Added

- **Every out-of-sequence HSM_LOAD chunk is proven to drop the pending
  envelope** (#50). A unit test sends the in-sequence chunk after a step back,
  a changed total and a chunk past the total, and sees each refused, so a
  refused chunk can never leave half an envelope for a later chunk to complete.
