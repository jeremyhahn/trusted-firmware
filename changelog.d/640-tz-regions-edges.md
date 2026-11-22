### Added

- **Host tests for the SAU checker's edges** (#640). `test_tz_regions` now
  checks, on both TZ=1 boards, that `tz_regions_check` takes a table of
  exactly eight regions and NS regions that touch the Secure code, the NSC
  and the Secure RAM without sharing a byte, and refuses an NSC region below
  the Secure code and a table with two NSC regions.
