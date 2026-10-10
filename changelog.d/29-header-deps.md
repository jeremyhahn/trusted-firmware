### Changed

- **Host and AVR objects rebuild when an included header changes** (#29). An
  edited header used to leave stale objects and test binaries behind, because a
  target depended on its `.c` files and nothing else.
  - Host binaries are compiled and linked by one `cc` command over many sources,
    where gcc's `-MMD` keeps only the last source's headers. `mk/ccdeps.sh`
    wraps that command and writes `<output>.d` with `-MM -MP -MT`, naming every
    header of every source; a deleted header is not a build error.
  - The AVR firmware and tfw-boot objects (`mk/firmware.mk`, `mk/boot.mk`) read
    their per-object `.d` files the same way. All of them are read with
    `-include`, after every rule, because the first build has none.

### Added

- **`make header-deps-check`** (#29), part of `make ci`. For `tfwsim`, a
  unit test and a boot test it asserts the binary is up to date after a build,
  touches one header it includes, and fails unless the binary is then out of
  date. The header's mtime is restored on every exit path. The motivating case
  was a stale `tfwsim` that answered protocol 1.0 after `protocol.h` had moved
  to 2.
