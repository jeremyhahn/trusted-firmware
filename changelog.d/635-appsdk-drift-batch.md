### Fixed

- **The application SDK and DICE docs give the alias TBS as 400 bytes, and
  the app ADRs cite current lines** (#635). `TBS_LEN` is 400 and
  `TBS_PREFIX_LEN` 249 (249 + the 151-byte claims set), as `src/caps/alias.c`
  asserts; the pin-reservation and wire-commands ADRs now point at the lines
  they describe, including the allow-list now in `src/proto/link_eth.h`.
