### Added

- **The Pico 2 W board test checks each PWM row is its own GPIO's slice and channel** (#621): `test_each_pwm_row_is_its_own_gpios_slice_and_no_slice_is_shared` reads every row of `boards/rp2350-pico2w/board.c`'s `tfw_board_pwms[]` and checks it names its pin's GPIO, slice `(n >> 1) & 7`, channel `n & 1` and port 0, and that the eight rows cover the eight slices once each, so a swapped or duplicated row now fails. Test only.
