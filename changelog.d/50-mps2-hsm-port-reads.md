### Changed

- **The mps2 tamper-response port's host test pins unknown actions and
  register reads** (#50). `make host-test` now checks that an action outside
  A1-A6 (0, 7, 0xFF) touches no register, that sense reads the button word
  once for both S1 and S5, and that power off, keys wipe and shutdown each do
  one read-modify-write of the LED word.
