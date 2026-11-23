### Changed

- **The rp2350-pico2w and nrf54l15-dk board pages mark slice 1 done** (#621).
  #500 and #529 landed them; the pages now name the PR and the test.
  `scripts/test_board_remaining_slices.py` (in `make scripts-test`) fails when
  an open "Remaining slices" item on any board page names a test already on
  disk.
