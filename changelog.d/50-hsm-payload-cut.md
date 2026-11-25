### Changed

- **The tamper-response payload reader's end-of-input guards are pinned**
  (#50). `make host-test` now checks that a policy set cut just before a
  head is refused with nothing past the cut read: a float64 lead byte past
  the cut, a set ending where a rule's response or its wipe begins, and an
  eight-byte head at the end with all four high zero bytes (too long a form)
  or only three of them (a truncation).
