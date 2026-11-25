### Changed

- **R7 counts every refusal `tfw_hsm_load` makes** (#50,
  `docs/hsm-self-defence/`), per owner ruling 1234: an envelope that fails
  verification, a set below the floor or one the reader refuses, a script the
  set does not name, a named script that does not read, and a script past the
  last slot each step the refusal count by one, with a host test per path. A
  dropped load chunk is still not counted.
