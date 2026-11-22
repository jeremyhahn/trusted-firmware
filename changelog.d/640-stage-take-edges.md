### Added

- **Host tests for the staged-image take's refusal and bound paths** (#640,
  row boards: open follow-ups). `test_boot_stage` and `test_boot_stage_2560`
  now drive a take with no store on the bus (refused, the MARK kept), a JEDEC
  id with only its maker byte blank, 00 or FF (taken as a present part), and a
  store that goes silent at the first measurement read (refused, nothing
  copied, the MARK kept).
